#ifndef MEMORYBITMAP_H
#define MEMORYBITMAP_H

#include <QVector>
#include <QPixmap>

class Memory;

class MemoryBitmap
{
public:
    // Describes what data is represented by a pixel.
    struct PixelInfo
    {
        bool isValid;       // is there a pixel here?
        int x;
        int y;
        QString pixelValue;     // currently: palette value, or "" if invalid
    };

    // Vector for palette
    typedef QVector<QRgb> Palette;

    MemoryBitmap();
    ~MemoryBitmap();

    // Access UI image, used for "Save Image".
    const QImage& GetImage() const { return m_img; }

    // Examine a single pixel
    void GetPixelInfo(int x, int y, PixelInfo& info) const;

    int width() const { return m_width; }
    int height() const { return m_height; }
    const QPixmap& pixmap() const { return m_pixmap; }

    void Clear();
    void Set1Plane(const Palette& palette, int strideInBytes, int height, const Memory* pMemOrig);
    void Set2Plane(const Palette& palette, int strideInBytes, int height, const Memory* pMemOrig);
    void Set3Plane(const Palette& palette, int strideInBytes, int height, const Memory* pMemOrig);
    void Set4Plane(const Palette& palette, int strideInBytes, int height, const Memory* pMemOrig);
    void Set8Plane(const Palette& palette, int strideInBytes, int height, const Memory* pMemOrig);
    void Set1BPP(int strideInBytes, int height, const Memory* pMemOrig);
    void SetTruColor(int strideInBytes, int height, const Memory* pMemOrig);
private:
    // How we store the pixmaps' data
    enum Mode
    {
        kIndexed,           // requires U8 data
        kTruColor           // requires RGB32 data (BB GG RR xx in memory)
    };

    // Enure that N bytes are allocated for main pixel data storage.
    // - indexed needs 1 byte/pixel
    // - TruColor need 4 bytes/pixel
    uint8_t* AllocPixelData(int size);

    // Set dimensions and refresh data from allocated bitmap area
    void SetPixmap(Mode mode, int width, int height);
    // Refresh m_img from the stored uint8_t* data/palette
    void RefreshPixmap();

    int             m_width;
    int             m_height;
    Mode            m_mode;

    // The "raw" temp buffer built up before making the QImage
    int             m_pixelDataSize;
    uint8_t*        m_pPixelData;

    // The colour palette
    Palette         m_colours;

    // Qt objects to be renderable
    QPixmap         m_pixmap;
    QImage          m_img;              // Qt wrapper for m_pPixelData
};

#endif // MEMORYBITMAP_H
