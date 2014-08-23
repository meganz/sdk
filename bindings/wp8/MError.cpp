#include "MError.h"

using namespace mega;
using namespace Platform;

MError::MError(MegaError *megaError, bool cMemoryOwn)
{
	this->megaError = megaError;
	this->cMemoryOwn = cMemoryOwn;
}

MError::~MError()
{
	if (cMemoryOwn)
		delete megaError;
}

MegaError* MError::getCPtr()
{
	return megaError;
}

MError^ MError::copy()
{
	return megaError ? ref new MError(megaError->copy(), true) : nullptr;
}

MErrorType MError::getErrorCode()
{
	return (MErrorType) (megaError ? megaError->getErrorCode() : 0);
}

String^ MError::getErrorString()
{
	std::string utf16error;
	const char *utf8error = megaError->getErrorString();
	MegaApi::utf8ToUtf16(utf8error, &utf16error);

	return ref new String((wchar_t *)utf16error.data());
}

int MError::getNextAttempt()
{
	return megaError ? megaError->getNextAttempt() : 0;
}

String^ MError::getErrorString(int errorCode)
{
	std::string utf16error;
	const char *utf8error = MegaError::getErrorString(errorCode);
	MegaApi::utf8ToUtf16(utf8error, &utf16error);
	delete[] utf8error;

	return ref new String((wchar_t *)utf16error.data());
}

String^ MError::toString()
{
	return getErrorString();
}
