#include "memorybitmap.h"

#include "../models/memory.h"

MemoryBitmap::MemoryBitmap() :
    m_pixelDataSize(0),
    m_pPixelData(nullptr)
{
    m_width = 0;
    m_height = 0;
    m_mode = kIndexed;
}

MemoryBitmap::~MemoryBitmap()
{
    delete [] m_pPixelData;
}

void MemoryBitmap::Set1Plane(const Palette& palette, int strideInBytes, int height, const Memory* pMemOrig)
{
    if (pMemOrig->GetSize() < (uint32_t)(strideInBytes * height))
    {
        Clear();
        return;
    }

    int numChunks = strideInBytes / 2;
    int width = numChunks * 16;
    int bitmapSize = numChunks * 16 * height;
    uint8_t* pDestPixels = AllocPixelData(bitmapSize);
    for (int y = 0; y < height; ++y)
    {
        const uint8_t* pChunk = pMemOrig->GetData() + y * strideInBytes;
        for (int x = 0; x < numChunks; ++x)
        {
            int32_t pSrc[1];
            pSrc[0] = (pChunk[0] << 8) | pChunk[1];
            for (int pix = 15; pix >= 0; --pix)
            {
                uint8_t val;
                val  = (pSrc[0] & 1);
                pDestPixels[pix] = val;
                pSrc[0] >>= 1;
            }
            pChunk += 2;
            pDestPixels += 16;
        }
    }
    m_colours = palette;
    SetPixmap(kIndexed, width, height);
}

void MemoryBitmap::Set2Plane(const Palette& palette, int strideInBytes, int height, const Memory* pMemOrig)
{
    if (pMemOrig->GetSize() < (uint32_t)(strideInBytes * height))
    {
        Clear();
        return;
    }

    int numChunks = strideInBytes / 4;
    int width = numChunks * 16;
    int bitmapSize = numChunks * 16 * height;
    uint8_t* pDestPixels = AllocPixelData(bitmapSize);
    for (int y = 0; y < height; ++y)
    {
        const uint8_t* pChunk = pMemOrig->GetData() + y * strideInBytes;
        for (int x = 0; x < numChunks; ++x)
        {
            int32_t pSrc[2];
            pSrc[1] = (pChunk[0] << 8) | pChunk[1];
            pSrc[0] = (pChunk[2] << 8) | pChunk[3];
            for (int pix = 15; pix >= 0; --pix)
            {
                uint8_t val;
                val  = (pSrc[0] & 1); val <<= 1;
                val |= (pSrc[1] & 1);
                pDestPixels[pix] = val;
                pSrc[0] >>= 1;
                pSrc[1] >>= 1;
            }
            pChunk += 4;
            pDestPixels += 16;
        }
    }
    m_colours = palette;
    SetPixmap(kIndexed, width, height);
}

void MemoryBitmap::Set3Plane(const Palette& palette, int strideInBytes, int height, const Memory* pMemOrig)
{
    if (pMemOrig->GetSize() < (uint32_t)(strideInBytes * height))
    {
        Clear();
        return;
    }
    int numChunks = strideInBytes / 6;
    int width = numChunks * 16;
    int bitmapSize = numChunks * 16 * height;
    uint8_t* pDestPixels = AllocPixelData(bitmapSize);
    for (int y = 0; y < height; ++y)
    {
        const uint8_t* pChunk = pMemOrig->GetData() + y * strideInBytes;
        for (int x = 0; x < numChunks; ++x)
        {
            int32_t pSrc[3];    // top 16 bits never used
            pSrc[2] = (pChunk[0] << 8) | pChunk[1];
            pSrc[1] = (pChunk[2] << 8) | pChunk[3];
            pSrc[0] = (pChunk[4] << 8) | pChunk[5];
            for (int pix = 15; pix >= 0; --pix)
            {
                uint8_t val;
                val  = (pSrc[0] & 1); val <<= 1;
                val |= (pSrc[1] & 1); val <<= 1;
                val |= (pSrc[2] & 1);

                pDestPixels[pix] = val;
                pSrc[0] >>= 1;
                pSrc[1] >>= 1;
                pSrc[2] >>= 1;
            }
            pChunk += 6;
            pDestPixels += 16;
        }
    }
    m_colours = palette;
    SetPixmap(kIndexed, width, height);
}


void MemoryBitmap::Set4Plane(const Palette& palette, int strideInBytes, int height, const Memory* pMemOrig)
{
    if (pMemOrig->GetSize() < (uint32_t)(strideInBytes * height))
    {
        Clear();
        return;
    }
    int numChunks = strideInBytes / 8;
    int width = numChunks * 16;
    int bitmapSize = numChunks * 16 * height;
    uint8_t* pDestPixels = AllocPixelData(bitmapSize);
    for (int y = 0; y < height; ++y)
    {
        const uint8_t* pChunk = pMemOrig->GetData() + y * strideInBytes;
        for (int x = 0; x < numChunks; ++x)
        {
            int32_t pSrc[4];    // top 16 bits never used
            pSrc[3] = (pChunk[0] << 8) | pChunk[1];
            pSrc[2] = (pChunk[2] << 8) | pChunk[3];
            pSrc[1] = (pChunk[4] << 8) | pChunk[5];
            pSrc[0] = (pChunk[6] << 8) | pChunk[7];
            for (int pix = 15; pix >= 0; --pix)
            {
                uint8_t val;
                val  = (pSrc[0] & 1); val <<= 1;
                val |= (pSrc[1] & 1); val <<= 1;
                val |= (pSrc[2] & 1); val <<= 1;
                val |= (pSrc[3] & 1);

                pDestPixels[pix] = val;
                pSrc[0] >>= 1;
                pSrc[1] >>= 1;
                pSrc[2] >>= 1;
                pSrc[3] >>= 1;
            }
            pChunk += 8;
            pDestPixels += 16;
        }
    }
    m_colours = palette;
    SetPixmap(kIndexed, width, height);
}

void MemoryBitmap::Set1BPP(int strideInBytes, int height, const Memory* pMemOrig)
{
    if (pMemOrig->GetSize() < (uint32_t)(strideInBytes * height))
    {
        Clear();
        return;
    }
    uint bitmapSize = strideInBytes * height;
    int width = strideInBytes;
    uint8_t* pDestPixels = AllocPixelData(bitmapSize);
    assert(pMemOrig->GetSize() >= bitmapSize);

    // This is a simple memcpy
    memcpy(pDestPixels, pMemOrig->GetData(), bitmapSize);

    // Fix greyscale palette for the moment
    m_colours.resize(256);
    for (uint i = 0; i < 256; ++i)
        m_colours[i] = (0xff000000 + i * 0x010101);

    SetPixmap(kIndexed, width, height);
}

void MemoryBitmap::SetTruColor(int strideInBytes, int height, const Memory* pMemOrig)
{
    int numWords = strideInBytes / 2;
    int width = numWords;

    int bitmapSize = width * 4 * height;
    uint8_t* pDestPixels = AllocPixelData(bitmapSize);
    for (int y = 0; y < height; ++y)
    {
        const uint8_t* pChunk = pMemOrig->GetData() + y * strideInBytes;
        for (int x = 0; x < width; ++x)
        {
            uint16_t pixVal = (pChunk[0] << 8) | pChunk[1];
            uint8_t r = ((pixVal >> 11) & 0x1f) << 3;
            uint8_t g = ((pixVal >>  5) & 0x3f) << 2;
            uint8_t b = ((pixVal >>  0) & 0x1f) << 3;
            *pDestPixels++ = b;
            *pDestPixels++ = g;
            *pDestPixels++ = r;
            *pDestPixels++ = 0xff;
            pChunk += 2;
        }
    }
    SetPixmap(kTruColor, width, height);
}


uint8_t* MemoryBitmap::AllocPixelData(int size)
{
    if (size == m_pixelDataSize)
        return m_pPixelData;

    delete [] m_pPixelData;
    m_pPixelData = new uint8_t[size];
    m_pixelDataSize = size;
    return m_pPixelData;
}

void MemoryBitmap::SetPixmap(Mode mode, int width, int height)
{
    m_mode = mode;
    m_width = width;
    m_height = height;
    RefreshPixmap();
}

void MemoryBitmap::Clear()
{
    SetPixmap(kIndexed, 0, 0);
}

void MemoryBitmap::RefreshPixmap()
{
    // Regenerate a new shape
    if (m_mode == kIndexed)
    {
        m_img = QImage(m_pPixelData, m_width, m_height, m_width, QImage::Format_Indexed8);
        m_img.setColorTable(m_colours);
    }
    else
    {
        m_img = QImage(m_pPixelData, m_width, m_height, QImage::Format_RGB32);
    }
    QPixmap pm = QPixmap::fromImage(m_img);
    m_pixmap = pm;
}

void MemoryBitmap::GetPixelInfo(int x, int y, PixelInfo& info) const
{
    info.x = x;
    info.y = y;
    info.pixelValue.clear();

    if (x >= 0 &&
        x < width() &&
        y >= 0 &&
        y < height() &&
        m_pPixelData)
    {
        if (m_mode == MemoryBitmap::kIndexed)
        {
            uint8_t val = m_pPixelData[y * m_width + x];
            info.pixelValue = QString::asprintf("%d", val);
        }
        else if (m_mode == MemoryBitmap::kTruColor)
        {
            const uint8_t* data = &m_pPixelData[(y * m_width + x) * 4];
            // Data is BB GG RR in memory
            info.pixelValue = QString::asprintf("[R:%d,G:%d,B:%d]",
                                                       data[2] >> 3,
                                                       data[1] >> 2,
                                                       data[0] >> 3);
        }
    }
    info.isValid = true;
}
