#include "stdafx.h"
#include "WbImperiHomeWebServer.h"
#include "../libs/libcomm/IPSupervisor.h"
#include "../libs/libcomm/Connection.h"

CWbImperiHomeWebServer::CWbImperiHomeWebServer(CConfigItem config)
	:m_sName("ImperiHome1"), m_MQConnection(NULL)
{
	m_Log = CLog::Default();
	m_MQTTServer = config.getStr("mqtt/host", true);
	m_WebPort = config.getInt("web/port", false, 80);

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
		}
	}

	CConfigItemList widgets;
	config.getList("widgets", widgets);
	int widgetNum=0;
	for_each_const(CConfigItemList, widgets, wgt)
	{
		widgetNum++;
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
			widget.topics["slot" + itoa(slotNum)] = (*slt)->getStr("");
			slotNum++;
		}
		m_Wigdets[widget.uid] = widget;

	}

/*	m_TypeMap["temperature"] = "DevTemperature";
	m_TypeMap["temp_humidity"] = "DevTempHygro";
	m_TypeMap["light"] = "DevLight";
	m_TypeMap["binary_sensor"] = "DevDoor";
	m_TypeMap["motion"] = "DevMotion";
	*/
}

CWbImperiHomeWebServer::~CWbImperiHomeWebServer()
{
	disconnect();
	loop_stop(true);
}

void CWbImperiHomeWebServer::Start()
{
	SetSupervisor(NULL);
	reinitialise(m_sName.c_str(), true);
	connect(m_MQTTServer.c_str());

	Listen(m_WebPort);
}

void CWbImperiHomeWebServer::LoadRooms()
{
	Json::Value devices;
	for_each(CWidgetsMap, m_Wigdets, i)
	{
		for_each(string_map, i->second.topics, topic)
		{
			subscribe(NULL, topic->second.c_str());
		}
	}
	subscribe(NULL, "/config/rooms/#");
	//subscribe(NULL, "/config/widgets/#");
}

void CWbImperiHomeWebServer::OnIdle()
{
	loop(1, 10);

	if (!m_MQConnection)
		reconnect();
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
		CWebServer::OnDeleteConnection(Conn);
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
		LoadRooms();
		//Announce("#Connected");
	}
}


void CWbImperiHomeWebServer::on_disconnect(int rc)
{
	//Announce("#Disonnected");
	m_isMQTTConnected = false;
	m_Log->Printf(1, "%s::on_disconnect(%d)", m_sName.c_str(), rc);
	m_MQConnection->SetAutoDelete();
	m_Supervisor->RemoveConnection(m_MQConnection);
	m_MQConnection = NULL;

}

void CWbImperiHomeWebServer::on_publish(int mid)
{
	m_Log->Printf(4, "%s::on_publish(%d)", m_sName.c_str(), mid);
}

void CWbImperiHomeWebServer::on_message(const struct mosquitto_message *message)
{
	m_Log->Printf(4, "%s::on_message(%s=%s)", m_sName.c_str(), message->topic, message->payload);

	string_vector v;
	SplitString(message->topic, '/', v);
	if (v.size() < 3)
		return;

	if (v[1] == "config")
	{
		if (v.size() == 5 && v[2] == "rooms")
		{
			if (v[4] == "name")
				m_Rooms[v[3]] = (char*)message->payload;
		}
		else if (v.size() == 5 && v[2] == "widgets")
		{
			if (v[4] == "name")
				m_Wigdets[v[3]].name = (char*)message->payload;
			else if (v[4] == "uid")
				m_Wigdets[v[3]].uid = (char*)message->payload;
			else if (v[4] == "template")
				m_Wigdets[v[3]].templ = (char*)message->payload;
			else if (v[4] == "room")
				m_Wigdets[v[3]].room = (char*)message->payload;
		}
		else if (v.size() == 7 && v[2] == "widgets" && v[4] == "controls" && v[6] == "topic")
		{
			m_Wigdets[v[3]].topics[v[5]] = (char*)message->payload;
			m_Topics[(char*)message->payload] = v[3];
			subscribe(NULL, (string((char*)message->payload)+"/#").c_str());
		}
	}
	else if (v.size() == 5 && v[1] == "devices")
	{
		m_Values[message->topic] = (char*)message->payload;
	}

	//	Announce(string(message->topic) + "=" + string((char*)message->payload, message->payloadlen));

}

void CWbImperiHomeWebServer::on_subscribe(int mid, int qos_count, const int *granted_qos)
{
	m_Log->Printf(10, "%s::on_subscribe(%d)", m_sName.c_str(), mid);
}

void CWbImperiHomeWebServer::on_unsubscribe(int mid)
{
	m_Log->Printf(4, "%s::on_message(%d)", m_sName.c_str(), mid);
}

void CWbImperiHomeWebServer::on_log(int level, const char *str)
{
	m_Log->Printf(level, "%s::on_log(%d, %s)", m_sName.c_str(), level, str);
}

void CWbImperiHomeWebServer::on_error()
{
	m_Log->Printf(1, "%s::on_error()", m_sName.c_str());

}


void addParam(Json::Value &params, string name, string value)
{
	Json::Value param;
	param["key"] = name;
	param["value"] = value;
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
	string replyStr;

	string_vector v;
	SplitString(url, '/', v);

	if (v[1] == "system")
	{
		replyJson["id"] = "WirenBoard";
		replyJson["apiversion"] = "1";
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
	}
	else if (v[1] == "devices")
	{
		if (v.size() < 5)
		{
			Json::Value devices;
			FillDevices(devices);
			replyJson["devices"] = devices;
		}
		else if (v.size() >= 6 && v[3]=="action")
		{
			Json::Value response;
			callAction(v[2], v[4], v[5], response);
			replyJson = response;
		}
	}

	string Reply, Body;
	Reply += "HTTP/1.1 200 OK\n";
	Reply += "Server: home/1.0.1\n";

	Reply += string("Date: ") + __DATE__ + "\n";
	Reply += "Content-Type: application/json\n";

	if (replyStr.length())
		Body = replyStr;
	else
		Body = Json::FastWriter().write(replyJson);
	
	Reply += "Content-Length: " + itoa(Body.length()) + "\n";
	//Reply += "Content-Type: Connection: keep-alive\n";


	Reply += "\n";
	Reply += Body;

	SendReply(Conn, Reply);
}

void CWbImperiHomeWebServer::FillDevices(Json::Value &devices)
{
	for_each(CWidgetsMap, m_Wigdets, i)
	{
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
			addParam(params, "Value", m_Values[i->second.topics["slot0"]]);
		}
		else
		{
			for_each_const(CTemplateControlVector, templ.controls, control)
			{
				if (control->type == "bool")
				{
					string val = m_Values[i->second.topics[control->slot]];
					if (atoi(val))
						addParam(params, control->paramName, "1");
					else
						addParam(params, control->paramName, "0");
				}
				else if (control->type == "percent")
				{
					string val = itoa(atof(m_Values[i->second.topics[control->slot]])/i->second.max*100);
					addParam(params, control->paramName, val);
				}
				else if (control->constVal.length())
					addParam(params, control->paramName, control->constVal);
				else if (control->values_map.size())
				{
					string wbVal = m_Values[i->second.topics[control->slot]];
					string_map::const_iterator i = control->values_map.find(wbVal);
					if (i != control->values_map.end())
					{
						addParam(params, control->paramName, i->second);
					}
				}
				else
					addParam(params, control->paramName, m_Values[i->second.topics[control->slot]]);
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
		else if (tmpl.type == "DevDimmer" || tmpl.type == "DevRGBLight")
		{
			string val = atoi(param) ? itoa(widget.max) : "0";
			publish(NULL, (widget.topics["slot0"] + "/on").c_str(), val.length(), val.c_str());
		}
		else if (tmpl.type == "DevLock" && false)  //not supported
		{
			string val = atoi(param) ? itoa(widget.max) : "0";
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
	else if (action == "setLevel")
	{
		if (tmpl.type == "DevDimmer" || tmpl.type == "DevShutter" || tmpl.type == "DevRGBLight")
		{
			string val = itoa(atoi(param) / 100.0 * widget.max);
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
