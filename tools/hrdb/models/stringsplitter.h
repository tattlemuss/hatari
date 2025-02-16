#ifndef STRINGSPLITTER_H
#define STRINGSPLITTER_H
#include <cstdint>
#include <string>
#include <vector>

//-----------------------------------------------------------------------------
class StringSplitter
{
public:
    explicit StringSplitter(const std::string& str) :
        m_str(str),
        m_pos(0)
    {
    }

    std::string Split(const char c)
    {
        if (m_pos == std::string::npos)
            return "";

        // Skip this char at the start
        //while (m_pos < m_str.size() && m_str[m_pos] == c)
        //	++m_pos;

        if (m_pos == m_str.size())
            return "";

        std::size_t start = m_pos;
        m_pos = m_str.find(c, m_pos);
        std::size_t endpos = m_pos;

        if (m_pos == std::string::npos)
            m_pos = endpos = m_str.size();
        else
        {
            // Skip any extra occurences of the char
            while (m_pos < m_str.size() && m_str[m_pos] == c)
                ++m_pos;
        }


        return m_str.substr(start, endpos - start);
    }

    uint32_t GetPos() const { return (uint32_t) m_pos; }
    char GetNext() { return m_str[m_pos++]; }

    void SplitAll(const char c, std::vector<std::string>& all)
    {
        all.clear();
        std::string next = this->Split(c);
        while (next != "")
        {
            all.push_back(next);
            next = this->Split(c);
        }
    }

private:
    const std::string&	m_str;
    std::size_t			m_pos;
};



#endif // STRINGSPLITTER_H
