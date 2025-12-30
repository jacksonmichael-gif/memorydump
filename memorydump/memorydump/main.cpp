#include <Windows.h>
#include <iostream>
#include <vector>
#include <variant>
#include <thread>
#include <mutex>
#include <algorithm>
#include <conio.h>
#include <chrono>

using namespace std;

//----------------------------
// 型定義
//----------------------------

using SearchValue = variant<int, float, double>;

struct FoundEntry {
    uintptr_t   addr;
    SearchValue lastValue;   // 前回スキャン時の値
};

struct RegionInfo {
    uintptr_t base;
    SIZE_T   size;
    DWORD    protect;
    DWORD    state;
    DWORD    type;
};

//----------------------------
// グローバル
//----------------------------

vector<RegionInfo> g_regions;         // 一度だけ列挙するメモリ領域
vector<FoundEntry> g_found;           // 現在の候補
mutex              g_foundMutex;

//----------------------------
// ユーティリティ
//----------------------------

bool isReadableRegion(const RegionInfo& r) {
    if (r.state != MEM_COMMIT) return false;
    if (r.protect & PAGE_NOACCESS) return false;
    if (r.protect & PAGE_GUARD) return false;

    // よく使われる読み書き属性に絞ると速い
    if (!(r.protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                       PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
        return false;
    }
    return true;
}

template<typename T>
bool readValue(HANDLE hProcess, uintptr_t addr, T& out) {
    return ReadProcessMemory(hProcess, (LPCVOID)addr, &out, sizeof(T), NULL);
}

// 比較: old < new なら 1 (増加), old > new なら -1 (減少), 同じなら 0
template<typename T>
int compareValues(const T& oldv, const T& newv) {
    if (newv > oldv) return 1;
    if (newv < oldv) return -1;
    return 0;
}

// SearchValue 同士の比較（同じ型前提）
int compareVariant(const SearchValue& oldv, const SearchValue& newv) {
    return visit([&](auto&& oldVal) -> int {
        using T = decay_t<decltype(oldVal)>;
        const T& newVal = get<T>(newv);
        return compareValues<T>(oldVal, newVal);
    }, oldv);
}

//----------------------------
// メモリ領域の列挙（1回だけ）
//----------------------------

void buildRegionList(HANDLE hProcess) {
    g_regions.clear();
    uintptr_t addr = 0;
    MEMORY_BASIC_INFORMATION mbi{};

    while (VirtualQueryEx(hProcess, (LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        RegionInfo r;
        r.base    = (uintptr_t)mbi.BaseAddress;
        r.size    = mbi.RegionSize;
        r.protect = mbi.Protect;
        r.state   = mbi.State;
        r.type    = mbi.Type;
        g_regions.push_back(r);

        addr = r.base + r.size;
    }

    cout << "regions: " << g_regions.size() << endl;
}

//----------------------------
// 領域1つをスキャン（テンプレート）
//----------------------------

template<typename T>
void scanRegionExact(
    HANDLE hProcess,
    const RegionInfo& r,
    T target,
    vector<FoundEntry>& localResults
) {
    if (!isReadableRegion(r)) return;

    vector<BYTE> buffer(r.size);
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(hProcess, (LPCVOID)r.base, buffer.data(), r.size, &bytesRead)) {
        return;
    }

    if (bytesRead < sizeof(T)) return;
    SIZE_T limit = bytesRead - sizeof(T);

    for (SIZE_T i = 0; i <= limit; ++i) {
        T v = *reinterpret_cast<T*>(buffer.data() + i);
        if (v == target) {
            FoundEntry e;
            e.addr      = r.base + i;
            e.lastValue = v;
            localResults.push_back(e);
        }
    }
}

// 範囲スキャン
template<typename T>
void scanRegionRange(
    HANDLE hProcess,
    const RegionInfo& r,
    T minv,
    T maxv,
    vector<FoundEntry>& localResults
) {
    if (!isReadableRegion(r)) return;

    vector<BYTE> buffer(r.size);
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(hProcess, (LPCVOID)r.base, buffer.data(), r.size, &bytesRead)) {
        return;
    }

    if (bytesRead < sizeof(T)) return;
    SIZE_T limit = bytesRead - sizeof(T);

    for (SIZE_T i = 0; i <= limit; ++i) {
        T v = *reinterpret_cast<T*>(buffer.data() + i);
        if (v >= minv && v <= maxv) {
            FoundEntry e;
            e.addr      = r.base + i;
            e.lastValue = v;
            localResults.push_back(e);
        }
    }
}

//----------------------------
// マルチスレッドで全領域スキャン
//----------------------------

template<typename T, typename ScanFunc>
void parallelScan(HANDLE hProcess, ScanFunc func) {
    g_found.clear();

    unsigned int threadCount = thread::hardware_concurrency();
    if (threadCount == 0) threadCount = 4;

    vector<thread> workers;

    for (unsigned int t = 0; t < threadCount; ++t) {
        workers.emplace_back([&, t]() {
            vector<FoundEntry> localResults;

            for (size_t i = t; i < g_regions.size(); i += threadCount) {
                func(g_regions[i], localResults);
            }

            lock_guard<mutex> lock(g_foundMutex);
            g_found.insert(g_found.end(), localResults.begin(), localResults.end());
        });
    }

    for (auto& th : workers) {
        th.join();
    }

    cout << "found count: " << g_found.size() << endl;
}

//----------------------------
// variant を使ったスキャンAPI
//----------------------------

void newScanExact(HANDLE hProcess, SearchValue value) {
    visit([&](auto&& v) {
        using T = decay_t<decltype(v)>;
        parallelScan<T>(hProcess, [&](const RegionInfo& r, vector<FoundEntry>& local) {
            scanRegionExact<T>(hProcess, r, v, local);
        });
    }, value);
}

void newScanRange(HANDLE hProcess, SearchValue minv, SearchValue maxv) {
    // min/max は同じ型前提
    visit([&](auto&& vmin) {
        using T = decay_t<decltype(vmin)>;
        T vmax = get<T>(maxv);
        parallelScan<T>(hProcess, [&](const RegionInfo& r, vector<FoundEntry>& local) {
            scanRegionRange<T>(hProcess, r, vmin, vmax, local);
        });
    }, minv);
}

//----------------------------
// 絞り込みスキャン（前回ヒットのみ）
//----------------------------

// 新しい値と「等しい」ものだけ残す
void refineEqual(HANDLE hProcess, SearchValue newValue) {
    vector<FoundEntry> next;

    visit([&](auto&& vNew) {
        using T = decay_t<decltype(vNew)>;

        for (auto& entry : g_found) {
            T now;
            if (!readValue<T>(hProcess, entry.addr, now)) continue;
            if (now == vNew) {
                FoundEntry e;
                e.addr      = entry.addr;
                e.lastValue = now;
                next.push_back(e);
            }
        }
    }, newValue);

    g_found.swap(next);
    cout << "equal count: " << g_found.size() << endl;
}

// 増加だけ残す
void refineIncrease(HANDLE hProcess) {
    vector<FoundEntry> next;

    for (auto& entry : g_found) {
        SearchValue newv;

        bool ok = visit([&](auto&& oldVal) -> bool {
            using T = decay_t<decltype(oldVal)>;
            T now;
            if (!readValue<T>(hProcess, entry.addr, now)) return false;
            newv = now;
            return true;
        }, entry.lastValue);

        if (!ok) continue;

        int cmp = compareVariant(entry.lastValue, newv);
        if (cmp == 1) { // 増加
            FoundEntry e;
            e.addr      = entry.addr;
            e.lastValue = newv;
            next.push_back(e);
        }
    }

    g_found.swap(next);
    cout << "increase count: " << g_found.size() << endl;
}

// 減少だけ残す
void refineDecrease(HANDLE hProcess) {
    vector<FoundEntry> next;

    for (auto& entry : g_found) {
        SearchValue newv;

        bool ok = visit([&](auto&& oldVal) -> bool {
            using T = decay_t<decltype(oldVal)>;
            T now;
            if (!readValue<T>(hProcess, entry.addr, now)) return false;
            newv = now;
            return true;
        }, entry.lastValue);

        if (!ok) continue;

        int cmp = compareVariant(entry.lastValue, newv);
        if (cmp == -1) { // 減少
            FoundEntry e;
            e.addr      = entry.addr;
            e.lastValue = newv;
            next.push_back(e);
        }
    }

    g_found.swap(next);
    cout << "decrease count: " << g_found.size() << endl;
}

// 変化したものだけ残す（増加 or 減少）
void refineChanged(HANDLE hProcess) {
    vector<FoundEntry> next;

    for (auto& entry : g_found) {
        SearchValue newv;

        bool ok = visit([&](auto&& oldVal) -> bool {
            using T = decay_t<decltype(oldVal)>;
            T now;
            if (!readValue<T>(hProcess, entry.addr, now)) return false;
            newv = now;
            return true;
        }, entry.lastValue);

        if (!ok) continue;

        int cmp = compareVariant(entry.lastValue, newv);
        if (cmp != 0) { // 変化
            FoundEntry e;
            e.addr      = entry.addr;
            e.lastValue = newv;
            next.push_back(e);
        }
    }

    g_found.swap(next);
    cout << "changed count: " << g_found.size() << endl;
}

// 変化していないものだけ残す
void refineUnchanged(HANDLE hProcess) {
    vector<FoundEntry> next;

    for (auto& entry : g_found) {
        SearchValue newv;

        bool ok = visit([&](auto&& oldVal) -> bool {
            using T = decay_t<decltype(oldVal)>;
            T now;
            if (!readValue<T>(hProcess, entry.addr, now)) return false;
            newv = now;
            return true;
        }, entry.lastValue);

        if (!ok) continue;

        int cmp = compareVariant(entry.lastValue, newv);
        if (cmp == 0) { // 不変
            FoundEntry e;
            e.addr      = entry.addr;
            e.lastValue = newv;
            next.push_back(e);
        }
    }

    g_found.swap(next);
    cout << "unchanged count: " << g_found.size() << endl;
}

//----------------------------
// UI 部分
//----------------------------

SearchValue inputValueByType(int type) {
    if (type == 1) {
        int v; cout << "int value: "; cin >> v; return v;
    } else if (type == 2) {
        float v; cout << "float value: "; cin >> v; return v;
    } else {
        double v; cout << "double value: "; cin >> v; return v;
    }
}

void showFoundSample() {
    size_t show = min<size_t>(g_found.size(), 20);
    for (size_t i = 0; i < show; ++i) {
        cout << hex << g_found[i].addr << dec << endl;
    }
    if (g_found.size() > show) {
        cout << "... (" << g_found.size() - show << " more)" << endl;
    }
}

void readMemoryFast(HANDLE hProcess) {
    if (g_regions.empty()) {
        buildRegionList(hProcess);
    }

    while (true) {
        cout << "\n--- scan menu ---\n";
        cout << "1: new scan (exact)\n";
        cout << "2: new scan (range)\n";
        cout << "3: refine: equal to value\n";
        cout << "4: refine: increased\n";
        cout << "5: refine: decreased\n";
        cout << "6: refine: changed\n";
        cout << "7: refine: unchanged\n";
        cout << "8: back to main\n";
        cout << "choice: ";

        int choice;
        cin >> choice;
        if (choice == 8) break;

        // 新規スキャン系は型入力が必要
        if (choice == 1 || choice == 2 || choice == 3) {
            int type;
            cout << "value type: 1:int 2:float 3:double : ";
            cin >> type;

            if (choice == 1) {
                SearchValue v = inputValueByType(type);
                newScanExact(hProcess, v);
                showFoundSample();
            }
            else if (choice == 2) {
                cout << "min value:\n";
                SearchValue vmin = inputValueByType(type);
                cout << "max value:\n";
                SearchValue vmax = inputValueByType(type);
                newScanRange(hProcess, vmin, vmax);
                showFoundSample();
            }
            else if (choice == 3) {
                if (g_found.empty()) {
                    cout << "no previous results. do a new scan first.\n";
                    continue;
                }
                SearchValue v = inputValueByType(type);
                refineEqual(hProcess, v);
                showFoundSample();
            }
        }
        else {
            // 型指定不要（前回の型情報を使う）
            if (g_found.empty()) {
                cout << "no previous results. do a new scan first.\n";
                continue;
            }
            if (choice == 4) {
                refineIncrease(hProcess);
                showFoundSample();
            }
            else if (choice == 5) {
                refineDecrease(hProcess);
                showFoundSample();
            }
            else if (choice == 6) {
                refineChanged(hProcess);
                showFoundSample();
            }
            else if (choice == 7) {
                refineUnchanged(hProcess);
                showFoundSample();
            }
        }
    }
}

//----------------------------
// write (とりあえず int 専用のまま)
//----------------------------

void writeMemory(HANDLE hProcess) {
    uintptr_t address;
    int type;

    cout << "value type to write: 1:int 2:float 3:double : ";
    cin >> type;

    // 値を型に応じて読み取る
    SearchValue newvalue = inputValueByType(type);

    cout << "enter address to write (hex): ";
    cin >> hex >> address >> dec;

    SIZE_T bytesWritten = 0;

    // variant を visit して型ごとに WriteProcessMemory
    bool ok = visit([&](auto&& v) -> bool {
        using T = decay_t<decltype(v)>;
        return WriteProcessMemory(
            hProcess,
            (LPVOID)address,
            &v,
            sizeof(T),
            &bytesWritten
        );
    }, newvalue);

    if (ok) {
        cout << "wrote " << bytesWritten << " bytes to 0x"
             << hex << address << dec << endl;
    } else {
        cout << "failed to write memory\n";
    }
}

//----------------------------
// main
//----------------------------
void monitormemory(HANDLE hProcess){
    cout << "Monitoring memory changes... (press any key to stop)\n";

    cout << "enter address to monitor (hex): ";
    uintptr_t address;
    cin >> hex >> address >> dec;

    cout << "enter value type to monitor: 1:int 2:float 3:double : ";
    int type;
    cin >> type;

    // 最初の値を読み取る
    SearchValue initialValue = inputValueByType(type);

    // 実際のメモリから読み取る
    visit([&](auto&& dummy) {
        using T = decay_t<decltype(dummy)>;
        T now;
        if (readValue<T>(hProcess, address, now)) {
            initialValue = now;
            cout << "value at 0x" << hex << address << dec << endl;
        } else {
            cout << "Failed to read initial value.\n";
        }
    }, initialValue);

    // 監視ループ
    while (true) {

        // キーが押されたら停止
        if (_kbhit()) {
            cout << "\nStopped monitoring.\n";
            break;
        }

        // 現在値を読み取って比較
        visit([&](auto&& oldVal) {
            using T = decay_t<decltype(oldVal)>;
            T now;
            if (readValue<T>(hProcess, address, now)) {
                if (now != oldVal) {
                    printf("\033[1K");
                    printf("\r%d", now); // 同じ行に表示を更新
                    initialValue = now;
                }
            }
        }, initialValue);

        this_thread::sleep_for(chrono::seconds(1));
    }
}

int main() {
    int PID;
    cout << "enter target process ID: ";
    cin >> PID;

    HANDLE hprocess = OpenProcess(
        PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION,
        FALSE,
        PID
    );

    if (!hprocess) {
        cout << "failed to open process\n";
        return 1;
    }

    cout << "process opened. PID = " << PID << endl;

    while (true) {
        cout << "\n--- main menu ---\n";
        cout << "1: read (fast scanner)\n";
        cout << "2: write int\n";
        cout << "3: monitor memory\n";
        cout << "4: exit\n";
        cout << "choice: ";

        int cmd;
        cin >> cmd;

        if (cmd == 4) break;
        else if (cmd == 1) readMemoryFast(hprocess);
        else if (cmd == 2) writeMemory(hprocess);
        else if (cmd == 3) monitormemory(hprocess);
    }

    CloseHandle(hprocess);
    return 0;
}