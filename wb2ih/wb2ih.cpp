// wb2ih.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "../libs/libutils/Config.h"
#include "WbImperiHomeWebServer.h"



int main(int argc, char* argv[])
{
	bool bDaemon = false;
	string serverName;
	string mqttHost = "wirenboard";
	string configName = "wb2ih.json";

#ifndef WIN32
	int c;
	//~ int digit_optind = 0;
	//~ int aopt = 0, bopt = 0;
	//~ char *copt = 0, *dopt = 0;
	while ((c = getopt(argc, argv, "c:s:")) != -1) {
		//~ int this_option_optind = optind ? optind : 1;
		switch (c) {
		case 'c':
			configName = optarg;
			break;
		case 's':
			serverName = optarg;
			break;
}
	}
#endif

	if (!configName.length())
	{
		printf("Config not defined\n");
		return -1;
	}

	try
	{
		CConfig config;
		config.Load(configName);

		CConfigItem debug = config.getNode("debug");
		if (debug.isNode())
		{
			CLog::Init(&debug);
		}

		CLog* log = CLog::Default();

		CConfigItem rooms = config.getNode("rooms");
		if (rooms.isNode())
		{
		} 

		mqttHost = config.getStr("mqtt/host", false, mqttHost);

		CWbImperiHomeWebServer ihServer(config.getRoot(), serverName);
		ihServer.Start();

		while (1)
			Sleep(1000);
	}
	catch (CHaException ex)
	{
		CLog* log = CLog::Default();
		log->Printf(0, "Failed with exception %d(%s)", ex.GetCode(), ex.GetMsg().c_str());
	}
	return 0;
}

