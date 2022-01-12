#include "Log.h"

#ifdef _WIN32
#include "arcdps_structs.h"
#include "Exports.h"
#endif

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef LINUX
#include <pthread.h>
#endif
#ifdef _WIN32
#include <Windows.h>
#endif

#include <chrono>
#include <ctime>

namespace
{
void SetThreadNameLogThread()
{
#ifdef LINUX
	pthread_setname_np(pthread_self(), "spdlog-worker");
#elif defined(_WIN32)
	SetThreadDescription(GetCurrentThread(), L"spdlog-worker");
#endif
}
};

void Log_::LogImplementation_(const char* pComponentName, const char* pFunctionName, const char* pFormatString, ...)
{
	char buffer[1024];

	va_list args;
	va_start(args, pFormatString);
	vsnprintf(buffer, sizeof(buffer), pFormatString, args);
	va_end(args);

	Log_::LOGGER->debug("{}|{}|{}", pComponentName, pFunctionName, buffer);
}

#ifdef _WIN32
void Log_::LogImplementationArc_(const char* pComponentName, const char* pFunctionName, const char* pFormatString, ...)
{
	if (GlobalObjects::ARC_E3 == nullptr)
	{
		return;
	}

	char timeBuffer[128];
	int64_t milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	int64_t seconds = milliseconds / 1000;
	milliseconds = milliseconds % 1000;
	int64_t writtenChars = std::strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", std::localtime(&seconds));
	assert(writtenChars >= 0);

	char buffer[4096];
	writtenChars = snprintf(buffer, sizeof(buffer) - 1, "%s.%lli|%s|%s|", timeBuffer, milliseconds, pComponentName, pFunctionName);
	assert(writtenChars >= 0);
	assert(writtenChars < sizeof(buffer) - 1);

	va_list args;
	va_start(args, pFormatString);

	int writtenChars2 = vsnprintf(buffer + writtenChars, sizeof(buffer) - writtenChars - 1, pFormatString, args);
	assert(writtenChars2 >= 0);
	assert(writtenChars2 < (sizeof(buffer) - writtenChars - 1));
	buffer[writtenChars + writtenChars2] = '\n';
	buffer[writtenChars + writtenChars2 + 1] = '\0';

	va_end(args);

	GlobalObjects::ARC_E3(buffer);
}
#endif

void Log_::FlushLogFile()
{
	Log_::LOGGER->flush();
}

void Log_::Init(bool pRotateOnOpen, const char* pLogPath)
{
	if (Log_::LOGGER != nullptr)
	{
		LogW("Skipping logger initialization since logger is not nullptr");
		return;
	}

	Log_::LOGGER = spdlog::rotating_logger_mt<spdlog::async_factory>("arcdps_healing_stats", pLogPath, 128*1024*1024, 8, pRotateOnOpen);
	Log_::LOGGER->set_pattern("%b %d %H:%M:%S.%f %t %L %v");
	spdlog::flush_every(std::chrono::seconds(5));
}

// SetLevel can still be used after calling this, the sink levels and logger levels are different things - e.g if logger
// level is debug and sink level is trace then trace lines will not be shown
void Log_::InitMultiSink(bool pRotateOnOpen, const char* pLogPathTrace, const char* pLogPathInfo)
{
	if (Log_::LOGGER != nullptr)
	{
		LogW("Skipping logger initialization since logger is not nullptr");
		return;
	}

	spdlog::init_thread_pool(8192, 1, &SetThreadNameLogThread);

	auto debug_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(pLogPathTrace, 128*1024*1024, 8, pRotateOnOpen);
	debug_sink->set_level(spdlog::level::trace);
	auto info_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(pLogPathInfo, 128*1024*1024, 8, pRotateOnOpen);
	info_sink->set_level(spdlog::level::info);

	std::vector<spdlog::sink_ptr> sinks{debug_sink, info_sink};
	Log_::LOGGER = std::make_shared<spdlog::async_logger>("arcdps_healing_stats", sinks.begin(), sinks.end(), spdlog::thread_pool(), spdlog::async_overflow_policy::block);
	Log_::LOGGER->set_pattern("%b %d %H:%M:%S.%f %t %L %v");
	spdlog::register_logger(Log_::LOGGER);

	spdlog::flush_every(std::chrono::seconds(5));
}

void Log_::Shutdown()
{
	Log_::LOGGER = nullptr;
	spdlog::shutdown();
}

static std::atomic_bool LoggerLocked = false;
void Log_::SetLevel(spdlog::level::level_enum pLevel)
{
	if (pLevel < 0 || pLevel >= spdlog::level::n_levels)
	{
		LogW("Not setting level to {} since level is out of range", pLevel);
		return;
	}
	if (LoggerLocked == true)
	{
		LogW("Not setting level to {} since logger is locked", pLevel);
		return;
	}

	Log_::LOGGER->set_level(pLevel);
	LogI("Changed level to {}", pLevel);
}

void Log_::LockLogger()
{
	LoggerLocked = true;
	LogI("Locked logger");
}
