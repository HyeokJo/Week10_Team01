#include "EnginePCH.h"
#include "EngineLog.h"

#include <chrono>
#include <cstdio>
#include <mutex>

namespace
{
    // Sink가 하나도 없을 때(에셋 로딩 등 에디터 초기화 전) 찍힌 로그.
    // 첫 Sink가 붙을 때 한꺼번에 넘긴다. Sink가 끝내 붙지 않는 빌드(Game)에서 무한히 쌓이지 않도록 개수를 제한한다.
    // 파일은 Sink와 별개로 항상 기록하므로 이 버퍼와 관계없이 모든 로그가 남는다.
    struct FPendingLog
    {
        ELogVerbosity Verbosity;
        FString Message;
    };

    constexpr int32 MaxPendingLogs = 4096;

    // 현재 로그 1개 + backup 19개. 넘으면 가장 오래된 backup부터 지운다.
    constexpr size_t MaxLogFiles = 20;

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

    // 버퍼가 가득 차서 Sink에 넘기지 못하고 버린 로그 수. 첫 Sink가 붙을 때 경고로 알린다.
    int32& GetDroppedPendingLogCount()
    {
        static int32 DroppedCount = 0;
        return DroppedCount;
    }

    fs::path& GetLogFilePath()
    {
        static fs::path Path;
        return Path;
    }

    // TaskPool 작업에서도 로그를 찍으므로 파일과 버퍼 접근을 묶는다.
    // Sink가 OnLog 안에서 다시 로그를 찍을 수 있어 재진입이 되는 mutex를 쓴다.
    std::recursive_mutex& GetLogMutex()
    {
        static std::recursive_mutex Mutex;
        return Mutex;
    }

    FILE*& GetLogFile()
    {
        static FILE* File = nullptr;
        return File;
    }

    const char* GetVerbosityName(ELogVerbosity Verbosity)
    {
        switch (Verbosity)
        {
        case ELogVerbosity::Input:   return "Input";
        case ELogVerbosity::Info:    return "Info";
        case ELogVerbosity::Warning: return "Warning";
        case ELogVerbosity::Error:   return "Error";
        }
        return "Info";
    }

    // 언리얼과 같은 "YYYY.MM.DD-HH.MM.SS" 형식. 이름순 정렬이 곧 시간순 정렬이 된다.
    template <typename Duration>
    FString FormatLocalTime(std::chrono::sys_time<Duration> Time)
    {
        const auto Local = std::chrono::current_zone()->to_local(std::chrono::floor<std::chrono::seconds>(Time));
        return std::format("{:%Y.%m.%d-%H.%M.%S}", Local);
    }

    FString GetExecutableName()
    {
        wchar_t Buffer[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, Buffer, MAX_PATH);
        return fs::path(Buffer).stem().string();
    }

    // 직전 실행의 로그를 "<이름>-backup-<마지막 수정 시각>.log"로 바꾼다. 바꾸는 파일은 항상 하나뿐이다.
    void BackupPreviousLog(const fs::path& LogDir, const fs::path& LogPath, const FString& BaseName)
    {
        std::error_code Error;
        if (!fs::exists(LogPath, Error))
            return;

        const auto WriteTime = std::chrono::clock_cast<std::chrono::system_clock>(fs::last_write_time(LogPath, Error));
        const FString Stamp = FormatLocalTime(WriteTime);

        // 같은 초에 두 번 실행한 경우 이름이 겹치지 않게 번호를 붙인다
        fs::path BackupPath = LogDir / (BaseName + "-backup-" + Stamp + ".log");
        for (int32 Suffix = 1; fs::exists(BackupPath, Error); ++Suffix)
            BackupPath = LogDir / std::format("{}-backup-{}_{}.log", BaseName, Stamp, Suffix);

        fs::rename(LogPath, BackupPath, Error);
    }

    // 현재 로그 자리를 하나 남기고 backup이 MaxLogFiles - 1개를 넘으면 오래된 것부터 지운다.
    void PurgeOldBackups(const fs::path& LogDir, const FString& BaseName)
    {
        const FString Prefix = BaseName + "-backup-";

        std::error_code Error;
        TArray<fs::path> Backups;
        for (const fs::directory_entry& Entry : fs::directory_iterator(LogDir, Error))
        {
            const FString FileName = Entry.path().filename().string();
            if (Entry.is_regular_file(Error) && FileName.starts_with(Prefix) && Entry.path().extension() == ".log")
                Backups.Add(Entry.path());
        }

        const size_t MaxBackups = MaxLogFiles - 1;
        if (static_cast<size_t>(Backups.Num()) <= MaxBackups)
            return;

        std::sort(Backups.begin(), Backups.end());
        const size_t RemoveCount = static_cast<size_t>(Backups.Num()) - MaxBackups;
        for (size_t Index = 0; Index < RemoveCount; ++Index)
            fs::remove(Backups[static_cast<uint32>(Index)], Error);
    }

    void WriteToFile(ELogVerbosity Verbosity, const FString& Message)
    {
        FILE* File = GetLogFile();
        if (!File)
            return;

        const auto Now = std::chrono::system_clock::now();
        const auto Millis = std::chrono::duration_cast<std::chrono::milliseconds>(Now.time_since_epoch()).count() % 1000;

        // 명령 출력 등 끝에 줄바꿈을 붙여 보내는 로그가 있어 한 줄로 맞춘다
        size_t Length = Message.size();
        while (Length > 0 && (Message[Length - 1] == '\n' || Message[Length - 1] == '\r'))
            --Length;

        const FString Line = std::format("[{}.{:03}][{}] {}\n", FormatLocalTime(Now), Millis, GetVerbosityName(Verbosity), std::string_view(Message.data(), Length));
        std::fwrite(Line.data(), 1, Line.size(), File);

        // 크래시 직전 로그가 버퍼에 남아 사라지지 않도록 매 줄 내보낸다
        std::fflush(File);
    }
}

void FLog::Init()
{
    std::lock_guard Lock(GetLogMutex());
    if (GetLogFile())
        return;

    const FString BaseName = GetExecutableName();
    const fs::path LogDir = fs::path("Saved") / "Logs";
    const fs::path LogPath = LogDir / (BaseName + ".log");

    std::error_code Error;
    fs::create_directories(LogDir, Error);

    BackupPreviousLog(LogDir, LogPath, BaseName);
    PurgeOldBackups(LogDir, BaseName);

    // 공유 읽기를 허용해 실행 중에도 다른 프로그램으로 로그를 열어 볼 수 있게 한다
    GetLogFile() = _wfsopen(LogPath.c_str(), L"wb", _SH_DENYWR);
    if (GetLogFile())
    {
        GetLogFilePath() = fs::absolute(LogPath, Error);
        WriteToFile(ELogVerbosity::Info, std::format("Log file: {}", GetLogFilePath().string()));
    }
}

void FLog::Shutdown()
{
    std::lock_guard Lock(GetLogMutex());
    if (FILE*& File = GetLogFile())
    {
        WriteToFile(ELogVerbosity::Info, "Log file closed");
        std::fclose(File);
        File = nullptr;
    }
}

void FLog::AddSink(ILogSink* Sink)
{
    if (!Sink)
        return;

    std::lock_guard Lock(GetLogMutex());
    GetSinks().Add(Sink);

    // 넘기는 도중 Sink가 다시 로그를 찍어도 배열이 흔들리지 않도록 먼저 꺼내 둔다
    TArray<FPendingLog> PendingLogs = std::move(GetPendingLogs());
    GetPendingLogs().Empty();
    for (const FPendingLog& Pending : PendingLogs)
        Sink->OnLog(Pending.Verbosity, Pending.Message);

    // 버린 로그가 있었다면 Sink 화면에서 끊긴 걸 알 수 있게 파일 위치와 함께 경고를 남긴다
    if (int32& DroppedCount = GetDroppedPendingLogCount(); DroppedCount > 0)
    {
        const FString LogPath = GetLogFilePath().empty() ? FString("the log file") : GetLogFilePath().string();
        const FString Warning = std::format("{} log lines were dropped before the log window was ready (buffer limit {}). See {}",
            DroppedCount, MaxPendingLogs, LogPath);
        DroppedCount = 0;
        Emit(ELogVerbosity::Warning, Warning);
    }
}

void FLog::RemoveSink(ILogSink* Sink)
{
    std::lock_guard Lock(GetLogMutex());
    TArray<ILogSink*>& Sinks = GetSinks();
    for (int32 Index = Sinks.Num() - 1; Index >= 0; --Index)
        if (Sinks[Index] == Sink)
            Sinks.RemoveAt(Index, 1);
}

void FLog::Emit(ELogVerbosity Verbosity, const FString& Message)
{
    std::lock_guard Lock(GetLogMutex());

    WriteToFile(Verbosity, Message);

    TArray<ILogSink*>& Sinks = GetSinks();
    if (Sinks.IsEmpty())
    {
        TArray<FPendingLog>& PendingLogs = GetPendingLogs();
        if (PendingLogs.Num() < MaxPendingLogs)
            PendingLogs.Add({ Verbosity, Message });
        else
            ++GetDroppedPendingLogCount();
        return;
    }

    for (ILogSink* Sink : Sinks)
        Sink->OnLog(Verbosity, Message);
}
