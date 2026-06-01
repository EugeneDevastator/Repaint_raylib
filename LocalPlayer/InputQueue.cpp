#include "InputQueue.h"

InputQueue g_inputQueue;

InputQueue::InputQueue() : m_head(0), m_tail(0) {}

void InputQueue::Clear() { m_head = m_tail = 0; }

void InputQueue::AddPoint(const InputPoint& pt) {
    int next = (m_tail + 1) % CAPACITY;
    if (next == m_head) return;
    m_buf[m_tail] = pt;
    m_tail = next;
}

int InputQueue::Drain(InputPoint* out, int maxOut) {
    int count = 0;
    while (m_head != m_tail && count < maxOut) {
        out[count++] = m_buf[m_head];
        m_head = (m_head + 1) % CAPACITY;
    }
    if (m_head == m_tail)
        m_head = m_tail = 0;
    return count;
}
