#include "tls.h"

size_t TlsBodyReader::readBytes(char *buffer, size_t length) {
    size_t got = 0;
    uint32_t last = millis();
    while (got < length) {
        int n = client.available() > 0 ? client.read((uint8_t *)buffer + got, length - got) : 0;
        if (n > 0) {
            got += n;
            last = millis();
        } else if (!client.connected() || millis() - last >= timeoutMs) {
            break;
        } else {
            vTaskDelay(1);
        }
    }
    return got;
}

int TlsBodyReader::read() {
    char c;
    return readBytes(&c, 1) == 1 ? (uint8_t)c : -1;
}
