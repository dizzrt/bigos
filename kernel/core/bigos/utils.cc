#include <bigos/utils.h>

char *bigos::itoa(int64_t _value, char *_buffer, uint8_t _radix) {
    if (_value == 0) {
        _buffer[0] = '0';
        _buffer[1] = '\0';
        return _buffer;
    }

    bool is_negative = false;
    uint64_t temp_value = _value;
    if (_value < 0) {
        is_negative = true;
        temp_value = -_value;
    }

    if (is_negative) {
        _buffer[0] = '-';
        return utoa(temp_value, _buffer + 1, _radix);
    } else
        return utoa(temp_value, _buffer, _radix);
}

char *bigos::utoa(uint64_t _value, char *_buffer, uint8_t _radix) {
    char c[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    if (_value == 0) {
        _buffer[0] = '0';
        _buffer[1] = '\0';
        return _buffer;
    }

    int offset = 0;
    do {
        _buffer[offset++] = c[_value % _radix];
        _value /= _radix;
    } while (_value);
    _buffer[offset] = '\0';

    char temp;
    for (int i = 0; i <= (offset - 1) / 2; i++) {
        temp = _buffer[i];
        _buffer[i] = _buffer[offset - i - 1];
        _buffer[offset - i - 1] = temp;
    }

    return _buffer;
}
