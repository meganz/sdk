#pragma once

#include "MLoggerInterface.h"
#include "megaapi.h"

namespace mega
{
	private class DelegateMLogger : public MegaLogger
	{
	public:
		DelegateMLogger(MLoggerInterface^ logger);
		void log(const char *time, int loglevel, const char *source, const char *message);

	private:
		MLoggerInterface^ logger;
	};
}
