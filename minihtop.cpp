#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <limits>
#include <ncurses.h>
#include <cstring>

using namespace std;

struct ProcInfo {
    int pid;
    string name;
    float cpu;
    long mem; // KB
};

// ====================== CPU ======================
struct CPUData {
    long long user, nice, sys, idle, iowait, irq, softirq, steal;
};

vector<CPUData> readCPUStats() {
    ifstream file("/proc/stat");
    vector<CPUData> cpus;
    string line;
    while (getline(file, line)) {
        if (line.rfind("cpu", 0) == 0) {
            CPUData cpu {};
            sscanf(line.c_str(), "cpu%*s %lld %lld %lld %lld %lld %lld %lld %lld",
                   &cpu.user, &cpu.nice, &cpu.sys, &cpu.idle,
                   &cpu.iowait, &cpu.irq, &cpu.softirq, &cpu.steal);
            cpus.push_back(cpu);
        } else break;
    }
    return cpus;
}

vector<float> getCPUUsage() {
    static vector<CPUData> last;
    vector<CPUData> current = readCPUStats();
    vector<float> usage;
    if (!last.empty()) {
        for (size_t i = 0; i < current.size(); i++) {
            long long user = current[i].user - last[i].user;
            long long nice = current[i].nice - last[i].nice;
            long long sys  = current[i].sys  - last[i].sys;
            long long idle = current[i].idle - last[i].idle;
            long long total = user + nice + sys + idle +
                              (current[i].iowait - last[i].iowait) +
                              (current[i].irq - last[i].irq) +
                              (current[i].softirq - last[i].softirq) +
                              (current[i].steal - last[i].steal);
            float percent = (total > 0) ? (float)(user + nice + sys) * 100.0f / total : 0.0;
            usage.push_back(percent);
        }
    } else usage.resize(current.size(), 0.0);
    last = current;
    return usage;
}

// ====================== MEMÓRIA ======================
float getMemoryUsage() {
    ifstream file("/proc/meminfo");
    if (!file.is_open()) return 0.0;
    string key;
    long totalMem = 0, freeMem = 0, buffers = 0, cached = 0;
    while (file >> key) {
        if (key == "MemTotal:") file >> totalMem;
        else if (key == "MemFree:") file >> freeMem;
        else if (key == "Buffers:") file >> buffers;
        else if (key == "Cached:") file >> cached;
        else file.ignore(numeric_limits<streamsize>::max(), '\n');
    }
    long usedMem = totalMem - (freeMem + buffers + cached);
    return (totalMem > 0) ? (float)usedMem / totalMem * 100.0f : 0.0;
}

// ====================== DISCO ======================
float getDiskUsage(const char* path = "/") {
    struct statvfs stat;
    if (statvfs(path, &stat) != 0) return 0.0;
    unsigned long long total = stat.f_blocks * stat.f_frsize;
    unsigned long long free = stat.f_bfree * stat.f_frsize;
    unsigned long long used = total - free;
    return (total > 0) ? (float)used / total * 100.0f : 0.0;
}

// ====================== PROCESSOS ======================
vector<ProcInfo> getTopProcesses(int topN = 5) {
    vector<ProcInfo> procs;
    DIR* dir = opendir("/proc");
    if (!dir) return procs;

    struct dirent* entry;
    long pageSize = sysconf(_SC_PAGESIZE);
    long clkTck = sysconf(_SC_CLK_TCK);

    // uptime do sistema
    double uptime = 0;
    {
        ifstream f("/proc/uptime");
        if (f.is_open()) f >> uptime;
    }

    while ((entry = readdir(dir)) != nullptr) {
        if (!isdigit(entry->d_name[0])) continue;
        int pid = atoi(entry->d_name);

        string statPath = string("/proc/") + entry->d_name + "/stat";
        string statusPath = string("/proc/") + entry->d_name + "/status";

        ifstream statFile(statPath);
        if (!statFile.is_open()) continue;

        string comm;
        char state;
        long utime, stime, cutime, cstime, starttime;
        long rss;
        string dummy;
        // Campos: https://man7.org/linux/man-pages/man5/proc.5.html
        statFile >> pid >> comm >> state;
        for (int i = 0; i < 10; i++) statFile >> dummy;
        statFile >> utime >> stime >> cutime >> cstime;
        for (int i = 0; i < 4; i++) statFile >> dummy;
        statFile >> starttime;
        statFile.close();

        double total_time = utime + stime;
        double seconds = uptime - (starttime / (double)clkTck);
        float cpu_usage = (seconds > 0) ? 100.0 * ((total_time / clkTck) / seconds) : 0.0;

        // Memória
        long mem_kb = 0;
        ifstream statusFile(statusPath);
        string key;
        while (statusFile >> key) {
            if (key == "VmRSS:") {
                statusFile >> mem_kb;
                break;
            } else statusFile.ignore(numeric_limits<streamsize>::max(), '\n');
        }

        // remove parênteses do comm
        if (comm.size() > 2 && comm[0] == '(') {
            comm = comm.substr(1, comm.size() - 2);
        }

        ProcInfo p {pid, comm, cpu_usage, mem_kb};
        procs.push_back(p);
    }
    closedir(dir);

    // ordenar por CPU
    sort(procs.begin(), procs.end(), [](const ProcInfo &a, const ProcInfo &b) {
        return a.cpu > b.cpu;
    });

    if ((int)procs.size() > topN) procs.resize(topN);
    return procs;
}

// ====================== BARRA ======================
void drawBar(int y, int x, float percent, const char* label, int width = 50) {
    int filled = (percent / 100.0) * width;
    mvprintw(y, x, "%s", label);

    int color = (percent < 50) ? 1 : (percent < 80) ? 2 : 3;

    attron(COLOR_PAIR(color));
    for (int i = 0; i < filled; i++) {
        mvprintw(y, x + (int)strlen(label) + 1 + i, "#");
    }
    attroff(COLOR_PAIR(color));

    for (int i = filled; i < width; i++) {
        mvprintw(y, x + (int)strlen(label) + 1 + i, "-");
    }
    mvprintw(y, x + (int)strlen(label) + 1 + width + 2, "%3.0f%%", percent);
}

// ====================== MAIN ======================
int main() {
    initscr();
    noecho();
    curs_set(FALSE);
    start_color();
    init_pair(1, COLOR_GREEN, COLOR_BLACK);
    init_pair(2, COLOR_YELLOW, COLOR_BLACK);
    init_pair(3, COLOR_RED, COLOR_BLACK);

    while (true) {
        clear();
        mvprintw(0, 0, "=== MINI-HTOP EM C++ ===   (CTRL+C para sair)");

        // CPUs
        vector<float> cpus = getCPUUsage();
        for (size_t i = 0; i < cpus.size(); i++) {
            string label;
            if (i == 0) label = "CPU Total:";
            else label = "CPU" + to_string(i - 1) + ":   ";
            drawBar(2 + i * 2, 2, cpus[i], label.c_str());
        }

        // Memória
        float mem = getMemoryUsage();
        drawBar(2 + cpus.size() * 2, 2, mem, "Mem:   ");

        // Disco
        float disk = getDiskUsage("/");
        drawBar(4 + cpus.size() * 2, 2, disk, "Disco: ");

        // Processos Top 5
        mvprintw(6 + cpus.size() * 2, 2, "Top 5 processos (CPU):");
        vector<ProcInfo> procs = getTopProcesses();
        for (size_t i = 0; i < procs.size(); i++) {
            mvprintw(7 + cpus.size() * 2 + i, 4, "%d %-15s CPU: %5.1f%%  Mem: %ld KB",
                     procs[i].pid, procs[i].name.c_str(), procs[i].cpu, procs[i].mem);
        }

        refresh();
        usleep(1000000); // 1s
    }

    endwin();
    return 0;
}
