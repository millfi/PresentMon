#pragma once

namespace pmon::gid
{
	inline constexpr const char* defaultControlPipeName = R"(\\.\pipe\fluentpresentmonsvcnamedpipe)";
	inline constexpr const char* defaultLogPipeBaseName = "fluent-pm-svc-log";
	inline constexpr const wchar_t* registryPath = LR"(SOFTWARE\FluentPresentMon\Service)";
	inline constexpr const char* middlewarePathKey = "sharedMiddlewarePath";
}
