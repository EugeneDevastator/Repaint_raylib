#include "InputQueue.h"

InputQueue g_inputQueue;

void InputQueue::AddEntry(const InputEntry& e) {
    int next = (m_tail + 1) % CAPACITY;
    if (next == m_head) return;
    m_buf[m_tail] = e;
    m_tail = next;
}

int InputQueue::Drain(InputEntry* out, int maxOut) {
    int count = 0;
    while (m_head != m_tail && count < maxOut) {
        out[count++] = m_buf[m_head];
        m_head = (m_head + 1) % CAPACITY;
    }
    if (m_head == m_tail) m_head = m_tail = 0;
    return count;
}
