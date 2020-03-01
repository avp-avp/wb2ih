#include "stdafx.h"
#include <ctime>
#include "WbImperiHomeWebServer.h"
#include "../libs/libcomm/IPSupervisor.h"
#include "../libs/libcomm/Connection.h"


Widget::Widget()
{
	device = NULL;
	max = -1;
}

CWbImperiHomeWebServer::CWbImperiHomeWebServer(CConfigItem config, string serverName)
	:m_sName(config.getStr("mqtt/name", false, "ImperiHome")), m_MQConnection(NULL), m_bLoaded(false), m_InsideLoop(0), m_bWebServerStarted(0)
{
#ifdef WIN32
	m_sName = string("ImperiHomeW32");
#endif
	if (serverName.length() > 0)
		m_sName = serverName;

	time(&m_LastOnIdle);
	m_Log = CLog::Default();
	m_MQTTServer = config.getStr("mqtt/host", true);
	m_WebPort = config.getInt("web/port", false, 80);
	m_WebHost = config.getStr("web/host", false, "");
	m_loadRooms = config.getInt("general/load_rooms", false, true)!=0;
	m_loadWidgets = config.getInt("general/load_widgets", false, true)!=0;
	m_RpcTopic = "/rpc/v1/db_logger/history/get_values/wb2ih_" + m_sName;

	CConfigItemList templates;
	config.getList("templates", templates);
	for_each_const(CConfigItemList, templates, tmpl)
	{
		string name = (*tmpl)->getStr("template");
		string typ = (*tmpl)->getStr("type");
		m_Templates[name].type=typ;

		CConfigItemList controls;
		(*tmpl)->getList("controls", controls);
		int slot = 0;
		for_each_const(CConfigItemList, controls, ctrl)
		{
			try
			{
				if ((*ctrl)->isStr())
				{
					TemplateControl control;
					control.paramName = (*ctrl)->getStr("");
					control.slot = "slot" + itoa(slot);
					m_Templates[name].controls.push_back(control);
				}
				else if ((*ctrl)->isNode())
				{
					TemplateControl control;
					control.paramName = (*ctrl)->getStr("name");
					control.slot = (*ctrl)->getStr("slot", false, "slot" + itoa(slot));
					control.constVal = (*ctrl)->getStr("const", false);
					CConfigItemList valuesMap;
					(*ctrl)->getList("values_map", valuesMap);
					for_each_const(CConfigItemList, valuesMap, valMap)
					{
						control.values_map[(*valMap)->getStr("wb")] = (*valMap)->getStr("ih");
					}
					control.type= (*ctrl)->getStr("type", false);
					if (control.type.length() == 0)
					{
						if (control.values_map.size() > 0)
							control.type = "values_map";
						else if (control.constVal.length()>0)
							control.type = "const";
						else
							control.type = "value";
					}

					m_Templates[name].controls.push_back(control);
				}
				slot++;
			} catch (CHaException ex) {
				m_Log->Printf(0, "Cannot load control for template %s", name.c_str());
				throw;
			}
		}

		CConfigItemList defaults;
		(*tmpl)->getList("default", defaults);
		for_each_const(CConfigItemList, defaults, dflt)
		{
			if ((*dflt)->isStr())
			{
				m_DefaultTemplates[(*dflt)->getStr("")] = name;
			}
		}
	}

	CConfigItemList widgets;
	config.getList("widgets", widgets);
	int widgetNum=0;
	for_each_const(CConfigItemList, widgets, wgt)
	{
		widgetNum++;

		try
		{
			Widget widget;
			widget.name = (*wgt)->getStr("name");
			widget.room = (*wgt)->getStr("room");
			widget.templ = (*wgt)->getStr("template");
			widget.uid = (*wgt)->getStr("uid", false, "custom_widget_"+itoa(widgetNum));
			widget.max = (*wgt)->getInt("max", false, 100);

			CConfigItemList slots;
			(*wgt)->getList("slots", slots);
			int slotNum = 0;
			for_each_const(CConfigItemList, slots, slt)
			{
				string topic = (*slt)->getStr("");
				widget.topics["slot" + itoa(slotNum)] = topic;
				widget.device = addDeviceFromTopic(topic);
				slotNum++;
			}
			m_Wigdets[widget.uid] = widget;
		} catch (CHaException ex) {
				m_Log->Printf(0, "Cannot load widgets number %d", widgetNum);
				throw;
		}
	}

	CConfigItemList rooms;
	config.getList("rooms", rooms);
	int roomNum = 0;
	for_each_const(CConfigItemList, rooms, room)
	{
		roomNum++;
		string roomName = "";
		int wgtNum;

		try
		{
			roomName = "";
			wgtNum = -1;
			string roomId = (*room)->getStr("id", false, "room_n" + itoa(roomNum));
			string name = (*room)->getStr("name", false, roomId);
			roomName = name;
			m_Rooms[roomId] = name;

			CConfigItemList widgets;
			(*room)->getList("widgets", widgets);
			wgtNum = 0;
			for_each_const(CConfigItemList, widgets, wgt)
			{
				wgtNum++;

				if ((*wgt)->isStr())
				{
					string control = (*wgt)->getStr("");
					string devName, ctrlName;
					try
					{
						SplitPair(control, '/', devName, ctrlName);
					}
					catch (CHaException ex)
					{
						m_Log->Printf(0, "Ivalid device '%s'. Skip", control.c_str());
						continue;
					}
					Widget widget;
					widget.uid = "device_" + itoa(roomNum)+"_"+itoa(wgtNum) + "_" + devName + "_" + ctrlName;
					widget.device = addDevice(control);
					widget.room = roomId;
					widget.controls.push_back(control);
					m_Wigdets[widget.uid] = widget;
				}
				else if ((*wgt)->isNode())
				{
					Widget widget;
					widget.uid = (*wgt)->getStr("id", false);
					widget.name = (*wgt)->getStr("name", false);
					widget.templ = (*wgt)->getStr("template", false);
					widget.max = (*wgt)->getInt("max", false, widget.max);
					widget.room = roomId;

					CConfigItemList devices;
					(*wgt)->getList("devices", devices);
					if (devices.size() > 0)
					{
						for_each_const(CConfigItemList, devices, control)
						{
							string device = (*control)->getStr("");
							string devName, ctrlName;
							try
							{
								SplitPair(device, '/', devName, ctrlName);
							}
							catch (CHaException ex)
							{
								m_Log->Printf(0, "Ivalid device '%s'. Skip", device.c_str());
								continue;
							}

							if (widget.uid.length()==0)
								widget.uid = "device_" + itoa(roomNum) + "_" + itoa(wgtNum) + "_" + devName + "_" + ctrlName;

							if (widget.device)
								widget.device = addDevice(device);
						}
					}
					else
					{
						string control = (*wgt)->getStr("device");
						string devName, ctrlName;
						try
						{
							SplitPair(control, '/', devName, ctrlName);
						}
						catch (CHaException ex)
						{
							m_Log->Printf(0, "Ivalid device '%s'. Skip", control.c_str());
							continue;
						}

						if (widget.uid.length() == 0)
							widget.uid = "device_" + itoa(roomNum) + "_" + itoa(wgtNum) + "_" + devName + "_" + ctrlName;

						widget.device = addDevice(control);
						widget.controls.push_back(control);
					}

					m_Wigdets[widget.uid] = widget;
				}
			}
		} catch (CHaException ex) {
				m_Log->Printf(0, "Cannot load widgets. Room %d(%s), widget %d", roomNum, roomName.c_str(), widgetNum);
				throw;
		}
	}
}

CWbImperiHomeWebServer::~CWbImperiHomeWebServer()
{
	disconnect();
	loop_stop(true);

	for_each(CWBDeviceMap, m_Devices, i)
	{
		delete i->second;
	}
	m_Devices.clear();
}

void CWbImperiHomeWebServer::Start()
{
	SetSupervisor(NULL);
	reinitialise(m_sName.c_str(), true);
	connect(m_MQTTServer.c_str());
	m_Supervisor->Start();

	m_Log->Printf(1, "Server %s started", m_sName.c_str());
}

void CWbImperiHomeWebServer::LoadWbConfiguration()
{
	string_vector v;
	for_each(CWBDeviceMap, m_Devices, device)
	{
		device->second->subscribeToEnrich(v);
	}

	for_each(string_vector, v, i)
	{
		m_Log->Printf(10, "Subscribe 1 '%s'", i->c_str());
		subscribe(NULL, i->c_str());
	}

	Json::Value devices;
	for_each(CWidgetsMap, m_Wigdets, i)
	{
		for_each(string_map, i->second.topics, topic)
		{
			m_Log->Printf(10, "Subscribe 2 '%s'", topic->second.c_str());
			subscribe(NULL, topic->second.c_str());
		}
	}


	if (m_loadRooms)
		subscribe(NULL, "/config/rooms/#");

	if (m_loadWidgets)
		subscribe(NULL, "/config/widgets/#");

	if (m_bLoaded) ConfigureWidgets();

	subscribe(NULL, (m_RpcTopic+"/reply").c_str());

}

void CWbImperiHomeWebServer::ConfigureWidgets(CWBDevice *device)
{
	bool haveUnloadedWidget = false;

	for_each(CWidgetsMap, m_Wigdets, wgt)
	{
		Widget &widget = wgt->second;

		if (device && widget.device != device)
			continue;

		if ((widget.templ == "" || widget.topics.size() == 0) && widget.device != NULL && widget.device->isLoaded())
		{
			if (haveUnloadedWidget)
			{
				m_Log->Printf(4, "widget %s not ready", widget.name.c_str());
				haveUnloadedWidget = true;
			}

			if (widget.name.length() == 0 && widget.device->getControls()->size()<2)
				widget.name = widget.device->getName();

			for_each(string_vector, widget.controls, ctrl)
			{
				string device, control;
				SplitPair(*ctrl, '/', device, control);
				const CWBControl *wbControl = widget.device->getControl(control);

				if (!wbControl)
					throw(CHaException(CHaException::ErrBadParam, "Unknown control '%s'", ctrl->c_str()));

				if (widget.name.length() == 0)
					widget.name = wbControl->Name;

				widget.max = wbControl->Max;
				string topic = widget.device->getTopic(control);
				widget.topics["slot" + itoa(widget.topics.size())] = topic;
				m_Log->Printf(10, "Subscribe 3 '%s'", topic.c_str());

				subscribe(NULL, topic.c_str());
				//widget.name = wbControl->Name;
				string type = wbControl->getTypeName(wbControl->Type);
				if (widget.templ.length())
				{
				}
				else if (m_DefaultTemplates.find(type) != m_DefaultTemplates.end())
				{
					widget.templ = m_DefaultTemplates[type];
				}
				else
				{
					m_Log->Printf(1, "No template for device %s (type '%s')", ctrl->c_str(), type.c_str());
				}
			}
		}
	}

	if (!haveUnloadedWidget)
		m_bLoaded = true;
}


void CWbImperiHomeWebServer::OnIdle()
{
	m_Log->Printf(8, "CWbImperiHomeWebServer::OnIdle");
	if (!m_InsideLoop)
		loop(1, 10);

	if (!m_MQConnection && socket() == INVALID_SOCKET)
		reconnect();

	if (!m_bLoaded)
	{
		m_Log->Printf(5, "Call ConfigureWidgets");
		ConfigureWidgets();
	}
	else if (!m_bWebServerStarted)
	{
		m_bWebServerStarted = true;
		Listen(m_WebPort, m_WebHost);
		m_Log->Printf(1, "Web server started on port %d", m_WebPort);
	}
	
	time(&m_LastOnIdle);

#ifdef WIN32
	if(!_CrtCheckMemory())
		m_Log->Printf(1, "_CrtCheckMemory fail", m_WebPort);
#endif
}

CWBDevice* CWbImperiHomeWebServer::addDevice(const string &control)
{
	string deviceName, ctrlName;
	try
	{
		SplitPair(control, '/', deviceName, ctrlName);
	}
	catch (CHaException ex)
	{
		throw CHaException(CHaException::ErrBadParam, "Bad control format '%s'", control.c_str());
	}

	CWBDevice *device = NULL;
	if (m_Devices.find(deviceName) == m_Devices.end())
	{
		device = new CWBDevice(deviceName, "");
		m_Devices[deviceName] = device;
	}
	else
		device = m_Devices[deviceName];

	device->addControl(ctrlName);
	return device;
}

CWBDevice* CWbImperiHomeWebServer::addDeviceFromTopic(const string &topic)
{
	// /devices/oregon_rx_1d20_15_1/controls/temperature/meta/readonly 1

	string_vector v;
	SplitString(topic, '/', v);
	if (v.size() < 3)
		return NULL;

	string deviceName = v[2];
	CWBDevice *device = NULL;
	if (m_Devices.find(deviceName) == m_Devices.end())
	{
		device = new CWBDevice(deviceName, "");
		m_Devices[deviceName] = device;
	}
	else
		device = m_Devices[deviceName];

	if (v.size() < 5)
		return device;

	string controlName = v[4];
	device->addControl(controlName);
	return device;
}


void CWbImperiHomeWebServer::OnRecieve(CConnection* Conn)
{
	if (Conn == m_MQConnection)
		loop(100, 100);
	else
		CWebServer::OnRecieve(Conn);
}

void CWbImperiHomeWebServer::OnDeleteConnection(CConnection* Conn)
{
	if (Conn == m_MQConnection)
	{
		m_MQConnection = NULL;
		m_Log->Printf(1, "%s::OnDeleteConnection(m_MQConnection)", m_sName.c_str());
	}
	else
	{
		CWebServer::OnDeleteConnection(Conn);
		for (CConnectionsMap::iterator i = m_Connections.begin(); i != m_Connections.end(); i++)
		{
			if (i->second == Conn)
			{
				m_Connections.erase(i);
				break;
			}
		}
	}
}

void CWbImperiHomeWebServer::on_connect(int rc)
{
	m_Log->Printf(1, "%s::on_connect(%d)", m_sName.c_str(), rc);

	if (!rc)
	{
		if (m_MQConnection)
		{
			m_MQConnection->SetAutoDelete();
			m_Supervisor->RemoveConnection(m_MQConnection);
		}

		m_MQConnection = new CIPConnection(socket(), "MQTT Connetion: " + m_MQTTServer);
		m_Supervisor->AddConnection(this, m_MQConnection);

		m_isMQTTConnected = true;
		LoadWbConfiguration();
		//Announce("#Connected");
	}
}


void CWbImperiHomeWebServer::on_disconnect(int rc)
{
	//Announce("#Disonnected");
	m_isMQTTConnected = false;
	m_Log->Printf(1, "%s::on_disconnect(%d)", m_sName.c_str(), rc);
	if (m_MQConnection)
	{
		m_MQConnection->SetAutoDelete();
		m_Supervisor->RemoveConnection(m_MQConnection);
		m_MQConnection = NULL;
	}
}

void CWbImperiHomeWebServer::on_publish(int mid)
{
	m_Log->Printf(4, "%s::on_publish(%d)", m_sName.c_str(), mid);
}

void CWbImperiHomeWebServer::on_message(const struct mosquitto_message *message)
{
	string payload;

	//m_Log->Printf(3, "%s::on_message(%s, %d bytes)", m_sName.c_str(), message->topic, message->payloadlen);

	if (message->payloadlen && message->payload)
	{
		char* payload_str = new char[message->payloadlen+1];
		memcpy(payload_str, message->payload, message->payloadlen);
		payload_str[message->payloadlen] = 0;
		//payload.assign((const char*)message->payload, message->payloadlen);
		//payload += "\0";
		payload = payload_str;
		//m_Log->Printf(3, "%s", payload_str);
		delete payload_str;
	}
	
	m_Log->Printf(5, "%s::on_message(%s=%s)", m_sName.c_str(), message->topic, payload.c_str());
	
	try
	{
		string_vector v;
		SplitString(message->topic, '/', v);
		if (v.size() < 3)
			return;

		if (v[1] == "config")
		{
			if (v.size() == 5 && v[2] == "rooms")
			{
				if (v[4] == "name")
					m_Rooms[v[3]] = payload;
			}
			else if (v.size() == 5 && v[2] == "widgets")
			{
				if (v[4] == "name")
					m_Wigdets[v[3]].name = payload;
				else if (v[4] == "uid")
					m_Wigdets[v[3]].uid = payload;
				else if (v[4] == "template")
					m_Wigdets[v[3]].templ = payload;
				else if (v[4] == "room")
					m_Wigdets[v[3]].room = payload;
			}
			else if (v.size() == 7 && v[2] == "widgets" && v[4] == "controls" && v[6] == "topic")
			{
				m_Wigdets[v[3]].topics[v[5]] = payload;
				m_Topics[payload] = v[3];
				CWBDevice *device = addDeviceFromTopic(payload);

				if (!device)
					throw CHaException(CHaException::ErrBadParam, "addDeviceFromTopic return NULL");

				m_Wigdets[v[3]].device = device;
				string_vector v;
				device->subscribeToEnrich(v);
				v.push_back(payload);
				for_each(string_vector, v, i)
				{
					m_Log->Printf(10, "Subscribe 4 '%s'", i->c_str());
					subscribe(NULL, i->c_str());
				}
			}
		}
		else if (v.size() == 5 && v[1] == "devices" && v[3] == "controls")
		{
			m_Values[message->topic] = payload;
		}
		else if (v.size() == 5 && v[1] == "devices" && v[3] == "meta")
		{
			if (m_Devices.find(v[2]) == m_Devices.end())
				throw CHaException(CHaException::ErrBadParam, "Unknown device '%s'", v[2].c_str());

			m_Devices[v[2]]->enrichDevice(v[4], payload);
		}
		else if (v.size() == 7 && v[1] == "devices"  && v[3] == "controls" && v[5] == "meta")
		{
			CWBDeviceMap::iterator i = m_Devices.find(v[2]);
			if (i == m_Devices.end())
				throw CHaException(CHaException::ErrBadParam, "Unknown device '%s'", v[2].c_str());
			
			bool isLoaded = i->second->isLoaded();
			i->second->enrichControl(v[4], v[6], payload);

			if (!isLoaded && i->second->isLoaded())
				ConfigureWidgets(i->second);
		}
		else if (v[1] == "rpc" && v[2] == "v1")
		{
			if (v.size() == 8 && v[3] == "db_logger" && v[4] == "history" && v[5] == "get_values" && v[7] == "reply")
			{
				m_Log->Printf(1, "Reply for %s: %s", v[6].c_str(), payload.c_str());
				Json::Reader reader;
				Json::Value mqtt_reply;
				bool parsingSuccessful = reader.parse(payload, mqtt_reply, false);
				if (!parsingSuccessful)
					throw CHaException(CHaException::ErrParsingError, "Failed to parse JSON reply from mqtt. Error %s", reader.getFormattedErrorMessages().c_str());
				long id = mqtt_reply["id"].asInt();
				Json::Value mqtt_values = mqtt_reply["result"]["values"], result, result_values;
				for (Json::ValueIterator i = mqtt_values.begin(); i != mqtt_values.end(); i++)
				{
					Json::Value value, v;
					value["date"] = Json::Int64((*i)["t"].asFloat()*1000);
					v = (*i)["v"];
					value["value"] = v.isString()?atof(v.asString()):v.asFloat();
					result_values.append(value);
				}
				result["values"] = result_values;

				CConnectionsMap::iterator it = m_Connections.find(id);
				if (it==m_Connections.end())
					throw CHaException(CHaException::ErrParsingError, "Connection with id %ld not found", id);
				SendHttpReplyJson(it->second, 200, result);
				it->second->AutoDelete();
				m_Connections.erase(it);
			}
		}
		else
			m_Log->Printf(4, "Uknown topic %s", message->topic);

		if (time(NULL) - m_LastOnIdle > 3)
		{
			m_InsideLoop++;
			OnIdle();
			m_InsideLoop--;
		}
	}
	catch (CHaException ex)
	{
		m_Log->Printf(0, "on_message(%s) Exception %d, %s", message->topic, ex.GetCode(), ex.GetMsg().c_str());
	}
	catch (std::exception ex)
	{
		m_Log->Printf(0, "on_message(%s) std::exception %s", message->topic, ex.what());
	}
}

void CWbImperiHomeWebServer::on_subscribe(int mid, int qos_count, const int *granted_qos)
{
	m_Log->Printf(10, "%s::on_subscribe(%d)", m_sName.c_str(), mid);
}

void CWbImperiHomeWebServer::on_unsubscribe(int mid)
{
	m_Log->Printf(4, "%s::on_unsubscribe(%d)", m_sName.c_str(), mid);
}

void CWbImperiHomeWebServer::on_log(int level, const char *str)
{
	m_Log->Printf(level, "%s::on_log(%d, %s)", m_sName.c_str(), level, str);
}

void CWbImperiHomeWebServer::on_error()
{
	m_Log->Printf(1, "%s::on_error()", m_sName.c_str());

}


void addParam(Json::Value &params, string name, string value, string unit="", bool graphable =true)
{
	Json::Value param;
	param["key"] = name;
	param["value"] = value;
	if (unit.length())	param["unit"] = unit;
	if (graphable)	param["graphable"] = true;
	params.append(param);
}

void addOption(Json::Value &params, string name, string value, string unit = "", bool graphable = true)
{
	Json::Value param;
	param["key"] = name;
	param["value"] = value;
	if (unit.length())	param["unit"] = unit;
	if (graphable)	param["graphable"] = true;
	params.append(param);
}

void CWbImperiHomeWebServer::OnRequest(CConnection* Conn, string method, string url, string_map &params)
{
	if (method != "GET")
	{
		CWebServer::OnRequest(Conn, method, url, params);
		return;
	}

	Json::Value replyJson;
	string_vector v;
	SplitString(url, '/', v);

	if (v.size() < 2)
	{ 
		SendHttpReply(Conn, 404, "Use Imperihome application");
	}
	else if (v[1] == "system")
	{
		replyJson["id"] = "WirenBoard";
		replyJson["apiversion"] = "1";
		SendHttpReplyJson(Conn, 200, replyJson);
	}
	else if (v[1] == "rooms")
	{
		Json::Value rooms;
		for_each(string_map, m_Rooms, i)
		{
			Json::Value room;
			room["id"] = i->first;
			room["name"] = "WB: "+i->second;
			rooms.append(room);
		}
		replyJson["rooms"] = rooms;
		SendHttpReplyJson(Conn, 200, replyJson);
	}
	else if (v[1] == "devices")
	{
		if (v.size() < 5)
		{
			Json::Value devices;
			FillDevices(devices);
			replyJson["devices"] = devices;
			SendHttpReplyJson(Conn, 200, replyJson);
		}
		else if (v.size() >= 6 && v[3] == "action")
		{
			Json::Value response;
			callAction(v[2], v[4], v[5], response);
			replyJson = response;
			SendHttpReplyJson(Conn, 200, replyJson);
		}
		else if (v.size() >= 6 && v[4] == "histo")
		{
			m_Log->Printf(1, "Request histo for '%s' '%s' %s-%s", v[2].c_str(), v[3].c_str(), v[5].c_str(), v[6].c_str());

			long long start = atoll(v[5].c_str()) / 1000;
			long long end = atoll(v[6].c_str()) / 1000;
			long long min_interval = end - start;
			CWidgetsMap::iterator wi = m_Wigdets.find(v[2]);
			if (wi == m_Wigdets.end())
			{
				m_Log->Printf(1, "device '%s' not found", v[2].c_str());
				SendHttpReply(Conn, 404, "device not found");
				return;
			}

			string deviceName, topic, controlName;
			Widget widget = wi->second;
			Template tmpl = m_Templates[widget.templ];
			CWBDevice *device = NULL;
			if (!widget.device)
			{
				m_Log->Printf(1, "device '%s' have no WBDevice", v[2].c_str());
				SendHttpReply(Conn, 404, "device not found");
				return;
			}

			device = widget.device;
			deviceName = device->getName();

			if (tmpl.controls.size() == 0)
			{
				if (widget.topics.size())
				{
					string_map::iterator i = widget.topics.begin();
					if (i!= widget.topics.end())
						topic = i->second;
				}
				else
				{
					m_Log->Printf(1, "device '%s', have no topics", v[2].c_str());
					SendHttpReply(Conn, 404, "device not found");
					return;
				}
			}
			else
			{
				for_each_const(CTemplateControlVector, tmpl.controls, control)
				{
					if (str_upper(control->paramName) == str_upper(v[3]))
					{
						topic = widget.topics[control->slot];
					}
				}

				if (!topic.length() && v[3]=="value" && widget.topics.size())
				{
					string_map::iterator i = widget.topics.begin();
					if (i != widget.topics.end())
						topic = i->second;
				}
			}

			if (topic.length())
			{
				string_vector v;
				SplitString(topic, "/", v);
				if (v.size() < 5)
				{
					m_Log->Printf(1, "device '%s', topic '%s' cannot extract control", v[2].c_str(), topic.c_str());
					SendHttpReply(Conn, 404, "invalid topic");
					return;
				}
				controlName = v[4];
				deviceName = v[2];
			}
			else
			{
				m_Log->Printf(1, "device '%s' cannot find topic for '%s'", v[2].c_str(), url.c_str());
				SendHttpReply(Conn, 404, "topic not found");
				return;
			}

			Json::Value request, params, channels, channel, timestamp;
			static Json::UInt id = 0;
			request["id"]=++id;
			channel.append(deviceName);
			channel.append(controlName);
			channels.append(channel);
			params["channels"] = channels;
			params["limit"] = 1000;
			params["ver"] = 1;
			timestamp["gt"] = Json::Int64(start);
			timestamp["lt"] = Json::Int64(end);
			params["timestamp"] = timestamp;
			params["min_interval"] = min_interval;
			request["params"] = params;
			string request_str = Json::FastWriter().write(request);

			publish(NULL, m_RpcTopic.c_str(), request_str.length(), request_str.c_str());
			m_Connections[id] = Conn;
		}
		else
		{
			m_Log->Printf(1, "Unknown request '%s' ", url.c_str());
			SendHttpReply(Conn, 404, "Not found");
		}
	}
	else
	{
		CWebServer::OnRequest(Conn, method, url, params);
	}
}

void CWbImperiHomeWebServer::SendHttpReplyJson(CConnection *con, int code, const Json::Value& body)
{
	SendHttpReply(con, code, Json::FastWriter().write(body));
}

void CWbImperiHomeWebServer::SendHttpReply(CConnection *con, int code, string body)
{
	string Reply, Body;
	Reply += string("HTTP/1.1 ")+itoa(code)+" OK\n";
	Reply += "Server: home/1.0.1\n";

	Reply += string("Date: ") + __DATE__ + "\n";
	Reply += "Content-Type: application/json\n";

	Reply += "Content-Length: " + itoa(body.length()) + "\n";
	//Reply += "Content-Type: Connection: keep-alive\n";


	Reply += "\n";
	Reply += body;

	SendReply(con, Reply);
}

void CWbImperiHomeWebServer::FillDevices(Json::Value &devices)
{
	for_each(CWidgetsMap, m_Wigdets, i)
	{
		if (i->second.templ.length() == 0)
			continue;

		if (m_Templates.find(i->second.templ) == m_Templates.end())
		{
			if (m_UnknownTemplates[i->second.templ].length() == 0)
			{
				m_UnknownTemplates[i->second.templ] = "1";
				m_Log->Printf(1, "Unknown template '%s'", i->second.templ.c_str());
			}
			continue;
		}

		Template templ = m_Templates[i->second.templ];
		Json::Value device;
		Json::Value params;
		device["id"] = i->second.uid;
		device["name"] = i->second.name;
		device["room"] = i->second.room;
		device["type"] = templ.type;
		//device["template"] = i->second.templ;
		if (templ.controls.size() == 0)
		{
			if (i->second.topics.find("slot0") != i->second.topics.end()) {
				addParam(params, "Value", m_Values[i->second.topics["slot0"]]);
			}
			else
				continue;
		}
		else
		{
			string unit;
			for_each_const(CTemplateControlVector, templ.controls, control)
				if (control->paramName == "unit")
					unit = control->constVal;

			for_each_const(CTemplateControlVector, templ.controls, control)
			{
				if (control->paramName == "unit")
					continue;
				else if (control->type == "bool")
				{
					string val = m_Values[i->second.topics[control->slot]];
					if (atoi(val))
						addParam(params, control->paramName, "1", unit);
					else
						addParam(params, control->paramName, "0", unit);
				}
				else if (control->type == "percent")
				{
					string val = itoa(0.5 + atof(m_Values[i->second.topics[control->slot]]) / i->second.max * 100);
					addParam(params, control->paramName, val, unit);
				}
				else if (control->type == "rgb")
				{
					string_vector v;
					string val = "";
					SplitString(m_Values[i->second.topics[control->slot]], ';', v);
					if (v.size() == 3){
						char buffer[32];
						snprintf(buffer, sizeof(buffer), "FF%02X%02X%02X", atoi(v[0]), atoi(v[1]), atoi(v[2]));
						val = buffer;
					}
					addParam(params, control->paramName, val, unit);
				}
				else if (control->constVal.length())
					addParam(params, control->paramName, control->constVal);
				else if (control->values_map.size())
				{
					string wbVal = m_Values[i->second.topics[control->slot]];
					string_map::const_iterator i = control->values_map.find(wbVal);
					if (i != control->values_map.end())
					{
						addParam(params, control->paramName, i->second, unit);
					}
				}
				else if (i->second.topics.size())
				{
					string topic = i->second.topics[control->slot];
					if (m_Values.find(topic) != m_Values.end())
						addParam(params, control->paramName, m_Values[topic], unit);
					else
					{
						m_Log->Printf(4, "No value for '%s' (%s)", topic.c_str(), control->paramName.c_str());
						continue;
					}
				}
				else
				{
					m_Log->Printf(4, "Unknown value type  for '%s'", control->paramName.c_str());
					continue;
				}
			}
		}
		device["params"] = params;
		devices.append(device);
	}
}
void CWbImperiHomeWebServer::callAction(string device, string action, string param, Json::Value &response)
{
	CWidgetsMap::iterator wi = m_Wigdets.find(device);
	if (wi == m_Wigdets.end())
	{
		response["success"] = false;
		response["errormsg"] = "device '"+device+"' not found";
		m_Log->Printf(1, "device '%s' not found", device.c_str());
		return;
	}

	Widget widget = wi->second;
	Template tmpl = m_Templates[widget.templ];

	if (action == "setStatus")
	{
		if (tmpl.type == "DevSwitch")
		{
			publish(NULL, (widget.topics["slot0"] + "/on").c_str(), param.length(), param.c_str());
		}
		else if (tmpl.type == "DevDimmer")
		{
			string val = atoi(param) ? itoa(widget.max) : "0";
			publish(NULL, (widget.topics["slot0"] + "/on").c_str(), val.length(), val.c_str());
		}
		else if (tmpl.type == "DevLock" && false)  //not supported
		{
			string val = atoi(param) ? itoa(widget.max) : "0";
			publish(NULL, (widget.topics["slot0"] + "/on").c_str(), val.length(), val.c_str());
		}
		else if (tmpl.type == "DevRGBLight")
		{
			if (widget.topics.size() > 1) {
				publish(NULL, (widget.topics["slot1"] + "/on").c_str(), param.length(), param.c_str());
			}
			else
			{
				string val = atoi(param) ? "255;255;255" : "0;0;0";
				publish(NULL, (widget.topics["slot0"] + "/on").c_str(), val.length(), val.c_str());
			}
		}
		else
		{
			response["success"] = false;
			response["errormsg"] = "action setStatus cannot be applied to '" + device + "'";
			m_Log->Printf(1, "action setStatus cannot be applied to '%s'", device.c_str());
			return;
		}
	} 
	else if (action == "setMode")
	{
		if (tmpl.type == "DevThermostat" )
		{
			for_each_const(CTemplateControlVector, tmpl.controls, control)
			{
				if (control->paramName == "curmode")
				{
					for_each_const(string_map, control->values_map, value)
					{
						if (value->second == param)
						{
							publish(NULL, (widget.topics[control->slot] + "/on").c_str(), value->first.length(), value->first.c_str());
						}
					}

				}
			}

		}
		else
		{
			response["success"] = false;
			response["errormsg"] = "action setMode cannot be applied to '" + device + "'";
			m_Log->Printf(1, "action setMode cannot be applied to '%s'", device.c_str());
			return;
		}

	}
	else if (action == "setSetPoint")
	{
		if (tmpl.type == "DevThermostat")
		{
			for_each_const(CTemplateControlVector, tmpl.controls, control)
			{
				if (control->paramName == "cursetpoint")
				{
					publish(NULL, (widget.topics[control->slot] + "/on").c_str(), param.length(), param.c_str());
				}
			}
		}
		else
		{
			response["success"] = false;
			response["errormsg"] = "action setSetPoint cannot be applied to '" + device + "'";
			m_Log->Printf(1, "action setSetPoint cannot be applied to '%s'", device.c_str());
			return;
		}

	}
	else if (action == "setLevel")
	{
		if (tmpl.type == "DevDimmer" || tmpl.type == "DevRGBLight")
		{
			string val = itoa(0.5 + atoi(param) / 100.0 * widget.max);
			publish(NULL, (widget.topics["slot0"] + "/on").c_str(), val.length(), val.c_str());
		}
		else if (tmpl.type == "DevShutter")
		{
			int val = atoi(param);
			publish(NULL, (widget.topics[val<50?"slot1":"slot0"] + "/on").c_str(), 1, "1");
		}
		else
		{
			response["success"] = false;
			response["errormsg"] = "action setStatus cannot be applied to '" + device + "'";
			m_Log->Printf(1, "action setStatus cannot be applied to '%s'", device.c_str());
			return;
		}

	}
	else if (action == "pulseShutter")
	{
		if (tmpl.type == "DevShutter")
		{
			if (param=="up" && widget.topics.size()>0)
				publish(NULL, (widget.topics["slot0"] + "/on").c_str(), 0, "");
			else if (param == "down" && widget.topics.size()>1)
				publish(NULL, (widget.topics["slot1"] + "/on").c_str(), 0, "");
			else
			{
				response["success"] = false;
				response["errormsg"] = "Invalid param '" + param + "' for action pulseShutter. Device '" + device + "'";
				m_Log->Printf(1, "%s", response["errormsg"].asCString());
			}
		}
		else
		{
			response["success"] = false;
			response["errormsg"] = "action setStatus cannot be applied to '" + device + "'";
			m_Log->Printf(1, "action setStatus cannot be applied to '%s'", device.c_str());
			return;
		}

	}
	else if (action == "stopShutter")
	{
		if (tmpl.type == "DevShutter")
		{
			if (widget.topics.size()>2)
				publish(NULL, (widget.topics["slot2"] + "/on").c_str(), 0, "");
			else
			{
				response["success"] = false;
				response["errormsg"] = "Invalid param '" + param + "' for action pulseShutter. Device '" + device + "'";
				m_Log->Printf(1, "%s", response["errormsg"].asCString());
			}
		}
		else
		{
			response["success"] = false;
			response["errormsg"] = "action setStatus cannot be applied to '" + device + "'";
			m_Log->Printf(1, "action setStatus cannot be applied to '%s'", device.c_str());
			return;
		}

	}
	else if (action == "setColor")
	{
		if (tmpl.type == "DevRGBLight")
		{
			int v, r, g, b;
#ifdef WIN32
			sscanf_s(param.c_str(), "%02X%02X%02X%02X", &v, &r, &g, &b);
#else
			sscanf(param.c_str(), "%02X%02X%02X%02X", &v, &r, &g, &b);
#endif
			string val = itoa(r) + ";" + itoa(g) + ";" + itoa(b);
			publish(NULL, (widget.topics["slot0"] + "/on").c_str(), val.length(), val.c_str());
		}
		else
		{
			response["success"] = false;
			response["errormsg"] = "action setStatus cannot be applied to '" + device + "'";
			m_Log->Printf(1, "action setStatus cannot be applied to '%s'", device.c_str());
			return;
		}

	}
	m_Log->Printf(3, "Call %s.%s(%s)", device.c_str(), action.c_str(), param.c_str());
	response["success"] = true;
	response["errormsg"] = "";
}
