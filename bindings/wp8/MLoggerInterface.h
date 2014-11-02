#pragma once

namespace mega
{
	using Platform::String;

	public interface class MLoggerInterface
	{
	public:
		void log(String ^time, int loglevel, String^source, String ^message);
	};
}
