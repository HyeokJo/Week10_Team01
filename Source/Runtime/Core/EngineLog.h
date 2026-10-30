#pragma once

enum class ELogVerbosity
{
	Input,
	Info,
	Warning,
	Error
};

class ILogSink
{
public:
	virtual void OnLog(ELogVerbosity, const FString&) = 0;
};

class FLog
{
public:
    // Saved/Logs/<실행 파일 이름>.log 를 연다. 직전 실행의 로그는 backup 이름으로 바꿔 두고, 오래된 backup은 지운다.
    static void Init();
    static void Shutdown();

    static void AddSink(ILogSink* Sink);
    static void RemoveSink(ILogSink* Sink);
    static void Emit(ELogVerbosity Verbosity, const FString& Message);

    template <typename... Args>
    static void Log(ELogVerbosity Verbosity, std::format_string<Args...> Fmt, Args&&... InArgs)
    {
        Emit(Verbosity, std::format(Fmt, std::forward<Args>(InArgs)...));
    }
};

#define HTR_LOG(Verbosity, ...) FLog::Log(ELogVerbosity::Verbosity, __VA_ARGS__)