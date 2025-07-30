#ifndef HOPPER56_BUFFER_H
#define HOPPER56_BUFFER_H

#include <cstdint>

namespace hop56
{
// ----------------------------------------------------------------------------
// Bounded access to a range of memory
class buffer_reader
{
public:
	buffer_reader(const uint8_t* pData, uint32_t length, uint32_t base_address) :
		m_pData(pData),
		m_length(length),
		m_baseAddress(base_address),
		m_pos(0)
	{}

	// Returns 0 for success, 1 for failure
	int read_word(uint32_t& data)
	{
		if (m_pos + 3 > m_length)
			return 1;
		data = m_pData[m_pos++];
		data <<= 8;
		data |= m_pData[m_pos++];
		data <<= 8;
		data |= m_pData[m_pos++];
		return 0;
	}

	void advance(uint32_t count)
	{
		m_pos += count * 3;
		if (m_pos > m_length)
			m_pos = m_length;
	}

	uint32_t get_pos() const
	{
		return m_pos / 3;
	}

	uint32_t get_address() const
	{
		return m_baseAddress + m_pos / 3;
	}

	uint32_t get_remain() const
	{
		return (m_length - m_pos) / 3;
	}

private:
	const uint8_t*  m_pData;
	const uint32_t  m_length;
	const uint32_t  m_baseAddress;
	uint32_t		m_pos;			// this is the byte position, we divide by 3 to get the word position.
};

}
#endif
