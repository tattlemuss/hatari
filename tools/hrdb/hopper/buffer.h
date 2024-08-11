#ifndef BUFFER_H
#define BUFFER_H

#include <cstdint>

namespace hopper68
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
	int read_byte(uint8_t& data)
	{
		if (m_pos + 1 > m_length)
			return 1;
		data = m_pData[m_pos++];
		return 0;
	}

	// Returns 0 for success, 1 for failure
	int read_word(uint16_t& data)
	{
		if (m_pos + 2 > m_length)
			return 1;
		data = m_pData[m_pos++];
		data <<= 8;
		data |= m_pData[m_pos++];
		return 0;
	}

	// Returns 0 for success, 1 for failure
	int read_long(uint32_t& data)
	{
		if (m_pos + 4 > m_length)
			return 1;
		data = m_pData[m_pos++];
		data <<= 8;
		data |= m_pData[m_pos++];
		data <<= 8;
		data |= m_pData[m_pos++];
		data <<= 8;
		data |= m_pData[m_pos++];
		return 0;
	}

	// Copy bytes into the buffer
	// Returns 0 for success, 1 for failure
	int read(uint8_t* data, int count)
	{
		if (m_pos + count > m_length)
			return 1;
		for (int i = 0; i < count; ++i)
			*data++ = m_pData[m_pos++];
		return 0;
	}

	void advance(uint32_t count)
	{
		m_pos += count;
		if (m_pos > m_length)
			m_pos = m_length;
	}

	void set_pos(uint32_t pos)
	{
		m_pos = pos;
		if (m_pos > m_length)
			m_pos = m_length;
	}

	const uint8_t* get_data() const
	{
		return m_pData + m_pos;
	}

	uint32_t get_pos() const
	{
		return m_pos;
	}

	uint32_t get_address() const
	{
		return m_baseAddress + m_pos;
	}

	uint32_t get_remain() const
	{
		return m_length - m_pos;
	}

private:
	const uint8_t*  m_pData;
	const uint32_t  m_length;
	const uint32_t  m_baseAddress;
	uint32_t		m_pos;
};

}
#endif
