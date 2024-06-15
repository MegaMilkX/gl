#include "log.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "platform/win32/module.hpp"
#include "filesystem/filesystem.hpp"


Log* Log::GetInstance() {
    static Log fl;
    return &fl;
}
void Log::Write(const char* category, const std::ostringstream& strm, Type type) {
    GetInstance()->_write(category, strm.str(), type);
}
void Log::Write(const char* category, const std::string& str, Type type) {
    GetInstance()->_write(category, str, type);
}


Log::Log()
: working(true) {
    thread_writer = std::thread([this](){
        tm ptm = {0};
        time_t t = time(0);
        localtime_s(&ptm, &t);
        char buffer[64];
        strftime(buffer, 64, "%d%m%Y", &ptm);
        std::string fname = win32GetThisModuleName() + "_" + std::string(buffer);

        fsCreateDirRecursive(fsGetModuleDir() + "\\log");

        std::ofstream f(fsGetModuleDir() + "\\log\\" + fname + ".log", std::ios::out | std::ios::app);

        do {
            std::queue<entry> lines_copy;
            {
                std::lock_guard<std::mutex> lock(sync);
                if(!working && lines.empty())
                    break;
                
                lines_copy = lines;
                while(!lines.empty()) {
                    lines.pop();
                }
            }
            while(!lines_copy.empty()) {
                entry e = lines_copy.front();
                tm ptm = {0};
                localtime_s(&ptm, &e.t);
                char buffer[32];
                strftime(buffer, 32, "%H:%M:%S", &ptm); 
                lines_copy.pop();

                HANDLE  hConsole;	
                hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                if(e.type == Log::Type::LOG_INFO) {
                    SetConsoleTextAttribute(hConsole, 7);
                } else if(e.type == Log::Type::LOG_WARN) {
                    SetConsoleTextAttribute(hConsole, 0xE);
                } else if(e.type == Log::Type::LOG_ERROR) {
                    SetConsoleTextAttribute(hConsole, 0xC);
                } else if(e.type == Log::Type::LOG_DEBUG_INFO) {
                    SetConsoleTextAttribute(hConsole, 10);
                }
                std::string str = static_cast<std::ostringstream&>(
                    std::ostringstream() << _typeToString(e.type) 
                    << "|" << buffer 
                    << "|" << std::hex << std::uppercase << e.thread_id
                    << "|" << e.category
                    << ": " << e.line 
                    << std::endl).str();
                f << str;
                std::cout << str;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } while(1);

        f << "\n\n\n";

        f.flush();
        f.close();
    });
}
Log::~Log() {
    working = false;
    if(thread_writer.joinable()) {
        thread_writer.join();
    }
}

void Log::_write(const char* category, const std::string& str, Type type) {
    std::lock_guard<std::mutex> lock(sync);
    lines.push(entry{
        type,
        time(0),
        GetCurrentThreadId(),
        category,
        str
    });
}

std::string Log::_typeToString(Type type) {
    std::string str;
    switch(type) {
    case LOG_INFO:
        str = "INFO";
        break;
    case LOG_WARN:
        str = "WARN";
        break;
    case LOG_ERROR:
        str = "ERR ";
        break;
    case LOG_DEBUG_INFO:
        str = "DINF";
        break;
    case LOG_DEBUG_WARN:
        str = "DWRN";
        break;
    case LOG_DEBUG_ERROR:
        str = "DERR";
        break;
    }
    return str;
}