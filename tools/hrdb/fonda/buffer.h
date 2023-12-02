#ifndef BUFFER_H
#define BUFFER_H

#include <cstdint>

// ----------------------------------------------------------------------------
// Bounded access to a range of memory
class buffer_reader
{
public:
	buffer_reader(const uint8_t* pData, uint64_t length) :
		m_pData(pData),
		m_length(length),
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

	// Templated read of a single object
	template<typename T>
		int read(T& data)
		{
			return read((uint8_t*)&data, sizeof(T));
		}

	void advance(uint64_t count)
	{
		m_pos += count;
		if (m_pos > m_length)
			m_pos = m_length;
	}

	int set(uint64_t pos)
	{
		m_pos = pos;
		if (m_pos > m_length)
			return 1;
		return 0;
	}

	const uint8_t* get_data() const
	{
		return m_pData + m_pos;
	}

	uint64_t get_pos() const
	{
		return m_pos;
	}

	uint64_t get_remain() const
	{
		return m_length - m_pos;
	}

private:
	const uint8_t*  m_pData;
	const uint64_t  m_length;
	uint64_t		m_pos;
};

#endif
