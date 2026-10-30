#include "EnginePCH.h"
#include "EngineLog.h"

namespace
{
    // Sink가 하나도 없을 때(에셋 로딩 등 에디터 초기화 전) 찍힌 로그.
    // 첫 Sink가 붙을 때 한꺼번에 넘긴다. Sink가 끝내 붙지 않는 빌드(Game)에서 무한히 쌓이지 않도록 개수를 제한한다.
    struct FPendingLog
    {
        ELogVerbosity Verbosity;
        FString Message;
    };

    constexpr int32 MaxPendingLogs = 4096;

    TArray<ILogSink*>& GetSinks()
    {
        static TArray<ILogSink*> Sinks;
        return Sinks;
    }

    TArray<FPendingLog>& GetPendingLogs()
    {
        static TArray<FPendingLog> PendingLogs;
        return PendingLogs;
    }
}

void FLog::AddSink(ILogSink* Sink)
{
    if (!Sink)
        return;

    GetSinks().Add(Sink);

    // 넘기는 도중 Sink가 다시 로그를 찍어도 배열이 흔들리지 않도록 먼저 꺼내 둔다
    TArray<FPendingLog> PendingLogs = std::move(GetPendingLogs());
    GetPendingLogs().Empty();
    for (const FPendingLog& Pending : PendingLogs)
        Sink->OnLog(Pending.Verbosity, Pending.Message);
}

void FLog::RemoveSink(ILogSink* Sink)
{
    TArray<ILogSink*>& Sinks = GetSinks();
    for (int32 Index = Sinks.Num() - 1; Index >= 0; --Index)
        if (Sinks[Index] == Sink)
            Sinks.RemoveAt(Index, 1);
}

void FLog::Emit(ELogVerbosity Verbosity, const FString& Message)
{
    TArray<ILogSink*>& Sinks = GetSinks();
    if (Sinks.IsEmpty())
    {
        TArray<FPendingLog>& PendingLogs = GetPendingLogs();
        if (PendingLogs.Num() < MaxPendingLogs)
            PendingLogs.Add({ Verbosity, Message });
        return;
    }

    for (ILogSink* Sink : Sinks)
        Sink->OnLog(Verbosity, Message);
}
