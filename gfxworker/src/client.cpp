#include "gfxworker/client.h"
#include "gfxworker/commands.h"
#include "gfxworker/command_serializer.h"
#include "mega/logging.h"

#include <thread>
#include <fstream>   

namespace gfx {
namespace client {

using gfx::comms::CommandNewGfx;
using gfx::comms::CommandNewGfxResponse;
using gfx::comms::CommandShutDown;
using gfx::comms::CommandShutDownResponse;
using gfx::comms::ProtocolWriter;
using gfx::comms::ProtocolReader;
using mega::LocalPath;

bool GfxClient::runShutDown()
{
	// connect
	auto endpoint = mComms->connect();
	if (!endpoint)
	{
		LOG_err << "GfxClient couldn't connect";
		return false;
	}

	// command
	CommandShutDown command;

	// send a request
	ProtocolWriter writer(endpoint.get());
	writer.writeCommand(&command, 5000);

	// get the response
	ProtocolReader reader(endpoint.get());
	auto response = reader.readCommand(5000);
	if (!dynamic_cast<CommandShutDownResponse*>(response.get()))
	{
		LOG_err << "GfxClient couldn't get response";
		return false;
	}
	else
	{
		LOG_info << "GfxClient gets shutdown response";
		return true;
	}
}

bool GfxClient::runGfxTask(const std::string& localpath)
{
	// connect
	auto endpoint = mComms->connect();
	if (!endpoint)
	{
		LOG_err << "GfxClient couldn't connect";
		return false;
	}

	// command
	CommandNewGfx command;
	command.Task.Path =  LocalPath::fromAbsolutePath(localpath).platformEncoded();
	command.Task.Sizes = std::vector<gfx::GfxSize>{ {250, 0} };

	// send a request
	ProtocolWriter writer(endpoint.get());
	writer.writeCommand(&command, 5000);

	// get the response
	ProtocolReader reader(endpoint.get());
	auto response = reader.readCommand(5000);
	CommandNewGfxResponse* addReponse = dynamic_cast<CommandNewGfxResponse*>(response.get());
	if (!addReponse)
	{
		LOG_err << "GfxClient couldn't get response";
		return false;
	}
	else
	{
		//std::string outputfile = "whatRandomName20030915_xxxxxx.jpeg";
		//std::ofstream out(outputfile.c_str(), std::ios::binary | std::ios::trunc);
		//if (out)
		//{
		//	out.write(addReponse->Images[0].c_str(), addReponse->Images[0].size());
		//}

		LOG_info << "GfxClient gets response, code " << addReponse->ErrorCode;
		return true;
	}
}

}
}
