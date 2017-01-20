#pragma once
#include "../libs/libcomm/WebServer.h"
#include "../libs/libutils/Config.h"
#include "../libs/libwb/WBDevice.h"
#include "mosquittopp.h"

struct Widget
{
	string name;
	string room;
	string templ;
	string uid;
	int max;
	string_map topics;
	string_vector controls;
	CWBDevice *device;

	Widget();
};
typedef map <string, Widget> CWidgetsMap;

struct TemplateControl
{
	string paramName;
	string constVal;
	string slot;
	string type;
	string_map values_map;
};
typedef vector<TemplateControl> CTemplateControlVector;

struct Template
{
	string type;
	CTemplateControlVector controls;
};
typedef map<string, Template> CTemplateMap;

class CWbImperiHomeWebServer :
	public CWebServer, mosqpp::mosquittopp
{
	string m_MQTTServer, m_sName;
	int m_WebPort;
	bool m_isMQTTConnected;
	CLog * m_Log;
	CConnection *m_MQConnection;
	int m_TopicRooms;
	string_map m_Rooms;
	CWidgetsMap m_Wigdets;
	string_map m_Topics;
	//string_map m_TypeMap;
	string_map m_Values;
	CTemplateMap m_Templates;
	string_map m_UnknownTemplates;
	bool m_loadRooms, m_loadWidgets;
	CWBDeviceMap m_Devices;
	bool Loaded;
	string_map m_DefaultTemplates;

public:
	CWbImperiHomeWebServer(CConfigItem config);
	~CWbImperiHomeWebServer();
	void Start();
	void LoadWbConfiguration();
	CWBDevice* addDeviceFromTopic(const string &topic);
	CWBDevice* addDevice(const string &topic);
	void ConfigureWidgets(CWBDevice *device = NULL);

private:
	virtual void on_connect(int rc);
	virtual void on_disconnect(int rc);
	virtual void on_publish(int mid);
	virtual void on_message(const struct mosquitto_message *message);
	virtual void on_subscribe(int mid, int qos_count, const int *granted_qos);
	virtual void on_unsubscribe(int mid);
	virtual void on_log(int level, const char *str);
	virtual void on_error();

	virtual void OnRecieve(CConnection* Conn);
	virtual void OnIdle();
	virtual void OnDeleteConnection(CConnection* Conn);

	virtual void OnRequest(CConnection* Conn, string method, string url, string_map &params);
	void FillDevices(Json::Value &devices);
	void callAction(string device, string action, string param, Json::Value &response);
};

