// TouchRate - minimal dependency-free gzip writer
//
// Produces a standard gzip stream (RFC 1952) carrying dynamic-Huffman DEFLATE
// (RFC 1951), so the output is readable by pandas, R, 7-Zip, zcat and anything
// else that speaks gzip. Text is buffered and compressed on Close().
#pragma once
#include "common.h"

class GzipWriter
{
public:
    ~GzipWriter() { Close(); }

    bool Open(const std::wstring& path);
    bool Close();                       // compresses and writes; false on error
    bool IsOpen() const { return m_open; }

    // Same surface as the plain-text writer it replaces.
    void S(const char* s);
    void P(const char* fmt, ...);

    uint64_t RawBytes() const { return (uint64_t)m_in.size(); }
    uint64_t CompressedBytes() const { return m_compressed; }

private:
    std::wstring          m_path;
    std::vector<uint8_t>  m_in;
    bool                  m_open = false;
    uint64_t              m_compressed = 0;
};

// Compress a buffer to a raw DEFLATE stream. Exposed for testing.
void Deflate(const uint8_t* data, size_t n, std::vector<uint8_t>& out);
uint32_t Crc32(const uint8_t* data, size_t n);
