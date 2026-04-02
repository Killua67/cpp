#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <vector>
#include <sstream>

// 跨平台串口头文件
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#endif

using namespace std;

// ====================== 1. 配置读取工具 ======================
struct ScaleConfig {
    string serialPort;
    int baudRate;
    int dataBits;
    int stopBits;
    string parity;
    int timeout;

    int frameLength;
    int signIndex;
    int valueStart;
    int valueEnd;
    int unitStart;
    int unitEnd;
};

// 读取config.ini配置文件
ScaleConfig loadConfig(const string& configPath) {
    ScaleConfig cfg{};
    ifstream file(configPath);
    string line;

    while (getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t pos = line.find('=');
        if (pos == string::npos) continue;

        string key = line.substr(0, pos);
        string val = line.substr(pos + 1);

        if (key == "SerialPort") cfg.serialPort = val;
        else if (key == "BaudRate") cfg.baudRate = stoi(val);
        else if (key == "DataBits") cfg.dataBits = stoi(val);
        else if (key == "StopBits") cfg.stopBits = stoi(val);
        else if (key == "Parity") cfg.parity = val;
        else if (key == "Timeout") cfg.timeout = stoi(val);
        else if (key == "FrameLength") cfg.frameLength = stoi(val);
        else if (key == "SignIndex") cfg.signIndex = stoi(val);
        else if (key == "ValueStart") cfg.valueStart = stoi(val);
        else if (key == "ValueEnd") cfg.valueEnd = stoi(val);
        else if (key == "UnitStart") cfg.unitStart = stoi(val);
        else if (key == "UnitEnd") cfg.unitEnd = stoi(val);
    }
    return cfg;
}

// ====================== 2. 数据解析工具 ======================
struct WeightInfo {
    double weight;
    string unit;
    bool isValid;
};

// 按配置解析电子秤原始数据
WeightInfo parseScaleData(const unsigned char* data, const ScaleConfig& cfg) {
    WeightInfo info{};
    info.isValid = false;

    // 1. 校验数据长度
    if (strlen((char*)data) != cfg.frameLength) {
        cerr << "[ERROR] 数据长度错误" << endl;
        return info;
    }

    try {
        // 2. 提取符号位
        bool isNegative = (data[cfg.signIndex] == '-');

        // 3. 提取重量数值
        int valueLen = cfg.valueEnd - cfg.valueStart + 1;
        char valueBuf[32] = {0};
        strncpy(valueBuf, (char*)data + cfg.valueStart, valueLen);
        double value = atof(valueBuf);
        if (isNegative) value = -value;

        // 4. 提取单位
        int unitLen = cfg.unitEnd - cfg.unitStart + 1;
        char unitBuf[8] = {0};
        strncpy(unitBuf, (char*)data + cfg.unitStart, unitLen);

        info.weight = value;
        info.unit = unitBuf;
        info.isValid = true;
    } catch (...) {
        cerr << "[ERROR] 数据解析失败" << endl;
    }
    return info;
}

// ====================== 3. 跨平台串口操作 ======================
#ifdef _WIN32
// Windows 串口初始化
HANDLE initSerialWindows(const ScaleConfig& cfg) {
    HANDLE hSerial = CreateFileA(cfg.serialPort.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hSerial == INVALID_HANDLE_VALUE) {
        cerr << "[ERROR] Windows串口打开失败" << endl;
        return INVALID_HANDLE_VALUE;
    }

    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    GetCommState(hSerial, &dcb);
    dcb.BaudRate = cfg.baudRate;
    dcb.ByteSize = cfg.dataBits;
    dcb.StopBits = (cfg.stopBits == 2) ? TWOSTOPBITS : ONESTOPBIT;

    if (cfg.parity == "ODD") dcb.Parity = ODDPARITY;
    else if (cfg.parity == "EVEN") dcb.Parity = EVENPARITY;
    else dcb.Parity = NOPARITY;

    SetCommState(hSerial, &dcb);
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadTotalTimeoutConstant = cfg.timeout;
    SetCommTimeouts(hSerial, &timeouts);

    return hSerial;
}
#else
// Linux 串口初始化
int initSerialLinux(const ScaleConfig& cfg) {
    int fd = open(cfg.serialPort.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        cerr << "[ERROR] Linux串口打开失败: " << strerror(errno) << endl;
        return -1;
    }

    struct termios options{};
    tcgetattr(fd, &options);
    cfsetispeed(&options, cfg.baudRate);
    cfsetospeed(&options, cfg.baudRate);

    options.c_cflag |= CLOCAL | CREAD;
    options.c_cflag &= ~CSIZE;
    if (cfg.dataBits == 7) options.c_cflag |= CS7;
    else options.c_cflag |= CS8;

    if (cfg.parity == "ODD") options.c_cflag |= PARENB | PARODD;
    else if (cfg.parity == "EVEN") options.c_cflag |= PARENB;
    else options.c_cflag &= ~PARENB;

    if (cfg.stopBits == 2) options.c_cflag |= CSTOPB;
    else options.c_cflag &= ~CSTOPB;

    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_oflag &= ~OPOST;

    tcsetattr(fd, TCSANOW, &options);
    return fd;
}
#endif

// ====================== 4. 主程序 ======================
int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // 1. 加载配置
    ScaleConfig cfg = loadConfig("config.ini");
    cout << "[OK] 配置加载完成，串口: " << cfg.serialPort << " 波特率: " << cfg.baudRate << endl;

    // 2. 初始化串口
#ifdef _WIN32
    HANDLE hSerial = initSerialWindows(cfg);
    if (hSerial == INVALID_HANDLE_VALUE) return -1;
#else
    int hSerial = initSerialLinux(cfg);
    if (hSerial < 0) return -1;
#endif

    cout << "[OK] 串口连接成功，开始实时读取称重数据..." << endl << endl;

    // 3. 循环读取数据
    unsigned char buffer[256] = {0};
    DWORD bytesRead;

    while (true) {
#ifdef _WIN32
        ReadFile(hSerial, buffer, cfg.frameLength, &bytesRead, NULL);
#else
        bytesRead = read(hSerial, buffer, cfg.frameLength);
#endif

        if (bytesRead == cfg.frameLength) {
            WeightInfo info = parseScaleData(buffer, cfg);
            if (info.isValid) {
                cout << "实时重量: " << info.weight << " " << info.unit << endl;
            }
        }
        memset(buffer, 0, sizeof(buffer));

#ifdef _WIN32
        Sleep(200);
#else
        usleep(200000);
#endif
    }

    // 关闭串口
#ifdef _WIN32
    CloseHandle(hSerial);
#else
    close(hSerial);
#endif
    return 0;
}