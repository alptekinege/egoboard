#pragma once

#include <QtGlobal>

/**
 * @brief Where a wheel scroll over the tray icon is inside the recent entries.
 *
 * Index 0 is the newest entry. The first scroll starts at the top (it must not
 * jump into the middle of the list), later scrolls walk towards older entries
 * and stop at the oldest instead of wrapping around — wrapping makes a fast
 * scroll land somewhere unpredictable. A new capture resets the walk.
 */
class TrayCycle {
public:
    // Positive steps go back in time (older), negative steps come forward.
    // Returns the resulting index, or -1 when there is nothing to cycle.
    int advance(int steps, int count)
    {
        if (count <= 0) {
            m_index = -1;
            return -1;
        }
        const int start = m_index < 0 ? 0 : m_index;
        m_index = qBound(0, start + steps, count - 1);
        return m_index;
    }

    // 1-based position for the "3 of 10" kind of hint; 0 when not cycling.
    int position() const { return m_index < 0 ? 0 : m_index + 1; }

    void reset() { m_index = -1; }

private:
    int m_index = -1;
};
