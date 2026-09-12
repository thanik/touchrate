#include "render.h"
#include <d3dcompiler.h>
#include <cstdarg>

static const char* kShaderSrc = R"HLSL(
cbuffer CB : register(b0) { float2 invViewport; float2 pad; };
struct VIn  { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };
struct VOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };

VOut VSMain(VIn i)
{
    VOut o;
    o.pos = float4(i.pos.x * invViewport.x * 2.0 - 1.0,
                   1.0 - i.pos.y * invViewport.y * 2.0, 0.0, 1.0);
    o.uv  = i.uv;
    o.col = i.col;
    return o;
}

Texture2D    tex  : register(t0);
SamplerState samp : register(s0);

float4 PSMain(VOut i) : SV_Target
{
    float a = tex.Sample(samp, i.uv).r;
    return float4(i.col.rgb, i.col.a * a);
}

// Used to composite the accumulated ink target, which carries real colour.
float4 PSMainRgba(VOut i) : SV_Target
{
    return tex.Sample(samp, i.uv) * i.col;
}
)HLSL";

static const size_t kVbCap = 65532;   // multiple of 6 so quads never straddle

template <class T> static void Rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

// ------------------------------------------------------------------- lifecycle

bool Renderer::Init(HWND hwnd, UINT dpi)
{
    m_hwnd = hwnd;
    m_dpi = dpi ? dpi : 96;
    m_scale = Clampf((float)m_dpi / 96.f, 1.f, 3.f);

    RECT rc{};
    GetClientRect(hwnd, &rc);
    m_w = std::max<int>(1, rc.right - rc.left);
    m_h = std::max<int>(1, rc.bottom - rc.top);

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    // Only if the debug layer is installed; retried without it on failure.
    UINT dbgFlags = flags | D3D11_CREATE_DEVICE_DEBUG;
#else
    UINT dbgFlags = flags;
#endif
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1
    };
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, dbgFlags,
                                   levels, _countof(levels), D3D11_SDK_VERSION,
                                   &m_dev, nullptr, &m_ctx);
    if (FAILED(hr) && dbgFlags != flags)
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                               levels, _countof(levels), D3D11_SDK_VERSION,
                               &m_dev, nullptr, &m_ctx);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                               levels, _countof(levels), D3D11_SDK_VERSION,
                               &m_dev, nullptr, &m_ctx);
    if (FAILED(hr)) return false;

    IDXGIDevice1* dxdev = nullptr;
    IDXGIAdapter* adapter = nullptr;
    if (SUCCEEDED(m_dev->QueryInterface(__uuidof(IDXGIDevice1), (void**)&dxdev)))
    {
        // One queued frame keeps the CPU from running ahead of the display.
        dxdev->SetMaximumFrameLatency(1);
        if (SUCCEEDED(dxdev->GetAdapter(&adapter)))
        {
            DXGI_ADAPTER_DESC ad{};
            if (SUCCEEDED(adapter->GetDesc(&ad))) m_adapter = WideToUtf8(ad.Description);
            adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&m_factory);
            adapter->Release();
        }
        dxdev->Release();
    }
    if (!m_factory && FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory2), (void**)&m_factory)))
        return false;

    IDXGIFactory5* f5 = nullptr;
    if (SUCCEEDED(m_factory->QueryInterface(__uuidof(IDXGIFactory5), (void**)&f5)))
    {
        BOOL allow = FALSE;
        if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                              &allow, sizeof allow)))
            m_tearing = allow == TRUE;
        f5->Release();
    }

    m_scFlags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    if (m_tearing) m_scFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = (UINT)m_w;
    sd.Height = (UINT)m_h;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = m_bufferCount;
    sd.Scaling = DXGI_SCALING_NONE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    sd.Flags = m_scFlags;

    hr = m_factory->CreateSwapChainForHwnd(m_dev, hwnd, &sd, nullptr, nullptr, &m_sc);
    if (FAILED(hr))
    {
        // Fall back to a plain flip chain if waitable/tearing is unavailable.
        m_scFlags = 0;
        m_tearing = false;
        sd.Flags = 0;
        hr = m_factory->CreateSwapChainForHwnd(m_dev, hwnd, &sd, nullptr, nullptr, &m_sc);
        if (FAILED(hr)) return false;
    }
    m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    if (SUCCEEDED(m_sc->QueryInterface(__uuidof(IDXGISwapChain2), (void**)&m_sc2)) && m_sc2)
    {
        if (m_scFlags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)
        {
            m_sc2->SetMaximumFrameLatency(1);
            m_maxLatency = 1;
            m_waitable = m_sc2->GetFrameLatencyWaitableObject();
        }
    }

    if (!CreatePipeline()) return false;
    if (!BuildAtlas()) return false;
    if (!CreateTargets()) return false;
    m_verts.reserve(kVbCap);
    return true;
}

void Renderer::Shutdown()
{
    ReleaseTargets();
    Rel(m_atlasSrv); Rel(m_atlas);
    Rel(m_samp); Rel(m_rs); Rel(m_blend);
    Rel(m_cb); Rel(m_vb);
    Rel(m_il); Rel(m_psRgba); Rel(m_ps); Rel(m_vs);
    Rel(m_sc2); Rel(m_sc);
    Rel(m_factory);
    Rel(m_ctx); Rel(m_dev);
    m_waitable = nullptr;
}

bool Renderer::CreatePipeline()
{
    ID3DBlob* vsb = nullptr, * psb = nullptr, * err = nullptr;
    UINT cf = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    if (FAILED(D3DCompile(kShaderSrc, strlen(kShaderSrc), "touchrate.hlsl", nullptr, nullptr,
                          "VSMain", "vs_4_0", cf, 0, &vsb, &err)))
    { Rel(err); return false; }
    if (FAILED(D3DCompile(kShaderSrc, strlen(kShaderSrc), "touchrate.hlsl", nullptr, nullptr,
                          "PSMain", "ps_4_0", cf, 0, &psb, &err)))
    { Rel(err); Rel(vsb); return false; }

    bool ok = SUCCEEDED(m_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &m_vs))
           && SUCCEEDED(m_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &m_ps));

    ID3DBlob* psRgba = nullptr;
    if (SUCCEEDED(D3DCompile(kShaderSrc, strlen(kShaderSrc), "touchrate.hlsl", nullptr, nullptr,
                             "PSMainRgba", "ps_4_0", cf, 0, &psRgba, &err)))
    {
        m_dev->CreatePixelShader(psRgba->GetBufferPointer(), psRgba->GetBufferSize(),
                                 nullptr, &m_psRgba);
        Rel(psRgba);
    }
    else Rel(err);

    const D3D11_INPUT_ELEMENT_DESC ie[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (ok) ok = SUCCEEDED(m_dev->CreateInputLayout(ie, _countof(ie),
                    vsb->GetBufferPointer(), vsb->GetBufferSize(), &m_il));
    Rel(vsb); Rel(psb);
    if (!ok) return false;

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = (UINT)(kVbCap * sizeof(Vtx));
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &m_vb))) return false;

    D3D11_BUFFER_DESC cd{};
    cd.ByteWidth = 16;
    cd.Usage = D3D11_USAGE_DYNAMIC;
    cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(m_dev->CreateBuffer(&cd, nullptr, &m_cb))) return false;

    D3D11_BLEND_DESC bl{};
    bl.RenderTarget[0].BlendEnable = TRUE;
    bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(m_dev->CreateBlendState(&bl, &m_blend))) return false;

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.ScissorEnable = TRUE;
    if (FAILED(m_dev->CreateRasterizerState(&rd, &m_rs))) return false;

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(m_dev->CreateSamplerState(&sd, &m_samp))) return false;
    return true;
}

bool Renderer::CreateTargets()
{
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(m_sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return false;
    HRESULT hr = m_dev->CreateRenderTargetView(bb, nullptr, &m_rtv);
    bb->Release();
    if (FAILED(hr)) return false;

    // Offscreen accumulation buffer for the persistent ink layer.
    D3D11_TEXTURE2D_DESC td{};
    td.Width = (UINT)m_w;
    td.Height = (UINT)m_h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (SUCCEEDED(m_dev->CreateTexture2D(&td, nullptr, &m_inkTex)))
    {
        if (FAILED(m_dev->CreateRenderTargetView(m_inkTex, nullptr, &m_inkRtv)) ||
            FAILED(m_dev->CreateShaderResourceView(m_inkTex, nullptr, &m_inkSrv)))
        {
            Rel(m_inkSrv); Rel(m_inkRtv); Rel(m_inkTex);
        }
        else
        {
            const float zero[4] = { 0, 0, 0, 0 };
            m_ctx->ClearRenderTargetView(m_inkRtv, zero);
        }
    }
    return true;
}

void Renderer::ReleaseTargets()
{
    Rel(m_inkSrv); Rel(m_inkRtv); Rel(m_inkTex);
    Rel(m_rtv);
}

void Renderer::BeginInk()
{
    if (!m_inkRtv) return;
    Flush();
    m_ctx->OMSetRenderTargets(1, &m_inkRtv, nullptr);
}

void Renderer::EndInk()
{
    Flush();
    m_ctx->OMSetRenderTargets(1, &m_rtv, nullptr);
}

void Renderer::ClearInk()
{
    if (!m_inkRtv) return;
    const float zero[4] = { 0, 0, 0, 0 };
    m_ctx->ClearRenderTargetView(m_inkRtv, zero);
}

void Renderer::CompositeInk()
{
    if (!m_inkSrv || !m_psRgba) return;
    Flush();
    m_ctx->PSSetShader(m_psRgba, nullptr, 0);
    m_ctx->PSSetShaderResources(0, 1, &m_inkSrv);
    Quad(0, 0, (float)m_w, (float)m_h, 0, 0, 1, 1, Color(1, 1, 1, 1));
    Flush();
    m_ctx->PSSetShader(m_ps, nullptr, 0);
    m_ctx->PSSetShaderResources(0, 1, &m_atlasSrv);
}

void Renderer::Resize(int w, int h)
{
    if (w <= 0 || h <= 0 || !m_sc) return;
    if (w == m_w && h == m_h && m_rtv) return;
    m_w = w; m_h = h;
    m_ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ReleaseTargets();
    m_sc->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, m_scFlags);
    CreateTargets();
}

void Renderer::SetDpi(UINT dpi)
{
    UINT d = dpi ? dpi : 96;
    if (d == m_dpi) return;
    m_dpi = d;
    m_scale = Clampf((float)m_dpi / 96.f, 1.f, 3.f);
    Rel(m_atlasSrv); Rel(m_atlas);
    BuildAtlas();
}

IDXGIOutput* Renderer::AcquireContainingOutput()
{
    IDXGIOutput* o = nullptr;
    if (m_sc && SUCCEEDED(m_sc->GetContainingOutput(&o))) return o;
    return nullptr;
}

// ----------------------------------------------------------------- font atlas

bool Renderer::BuildAtlas()
{
    const int px[F_COUNT] = {
        std::max(11, (int)lround(12.0 * m_scale)),
        std::max(13, (int)lround(15.0 * m_scale)),
        std::max(17, (int)lround(20.0 * m_scale)),
        std::max(34, (int)lround(46.0 * m_scale)),
    };

    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!dc) return false;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = m_atlasW;
    bi.bmiHeader.biHeight = -m_atlasH;       // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) { DeleteDC(dc); return false; }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    uint32_t* pix = (uint32_t*)bits;
    memset(pix, 0, (size_t)m_atlasW * m_atlasH * 4);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    SetTextAlign(dc, TA_LEFT | TA_TOP);

    int cx = 1, cy = 1, rowH = 0;
    auto newRow = [&](int h) {
        if (cx + 2 > m_atlasW || cy + h + 2 > m_atlasH) { cx = 1; cy += rowH + 2; rowH = 0; }
    };
    auto place = [&](int w, int h, int& ox, int& oy) -> bool {
        if (cx + w + 1 > m_atlasW) { cx = 1; cy += rowH + 2; rowH = 0; }
        if (cy + h + 1 > m_atlasH) return false;
        ox = cx; oy = cy;
        cx += w + 2;
        rowH = std::max(rowH, h);
        return true;
    };

    for (int f = 0; f < F_COUNT; ++f)
    {
        HFONT font = CreateFontW(-px[f], 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 ANTIALIASED_QUALITY, FF_DONTCARE, L"Consolas");
        if (!font)
            font = CreateFontW(-px[f], 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                               ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, nullptr);
        HGDIOBJ oldFont = SelectObject(dc, font);

        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        FontInfo& fi = m_fonts[f];
        fi.height = (float)tm.tmHeight;
        fi.ascent = (float)tm.tmAscent;
        fi.advance = (float)tm.tmAveCharWidth;

        cx = 1; cy += rowH + 2; rowH = 0;
        for (int ci = 0; ci < 95; ++ci)
        {
            wchar_t ch = (wchar_t)(32 + ci);
            SIZE sz{};
            GetTextExtentPoint32W(dc, &ch, 1, &sz);
            int gw = std::max<int>(1, sz.cx), gh = tm.tmHeight;

            int ox = 0, oy = 0;
            if (!place(gw, gh, ox, oy)) { fi.g[ci] = Glyph{}; continue; }
            if (ci != 0) TextOutW(dc, ox, oy, &ch, 1);

            Glyph& g = fi.g[ci];
            g.u0 = (float)ox / m_atlasW;      g.v0 = (float)oy / m_atlasH;
            g.u1 = (float)(ox + gw) / m_atlasW; g.v1 = (float)(oy + gh) / m_atlasH;
            g.w = (float)gw; g.h = (float)gh; g.adv = (float)sz.cx;
            if (ci == 'M' - 32) fi.advance = (float)sz.cx;
        }
        SelectObject(dc, oldFont);
        DeleteObject(font);
    }

    // ---- procedural shapes, drawn straight into the DIB
    cx = 1; cy += rowH + 2; rowH = 0;
    auto setShape = [&](Glyph& g, int size, auto&& fn) {
        int ox = 0, oy = 0;
        if (!place(size, size, ox, oy)) { g = Glyph{}; return; }
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                float a = Clampf(fn((float)x + 0.5f, (float)y + 0.5f, (float)size), 0.f, 1.f);
                uint32_t v = (uint32_t)(a * 255.f + 0.5f);
                pix[(size_t)(oy + y) * m_atlasW + (ox + x)] = (v << 16) | (v << 8) | v | 0xFF000000u;
            }
        g.u0 = (float)ox / m_atlasW;        g.v0 = (float)oy / m_atlasH;
        g.u1 = (float)(ox + size) / m_atlasW; g.v1 = (float)(oy + size) / m_atlasH;
        g.w = g.h = (float)size; g.adv = (float)size;
    };

    setShape(m_white, 8, [](float, float, float) { return 1.f; });
    setShape(m_disc, 128, [](float x, float y, float s) {
        float r = s * 0.5f - 1.f, c = s * 0.5f;
        float d = std::sqrt((x - c) * (x - c) + (y - c) * (y - c));
        return r - d + 0.5f;
    });
    setShape(m_ring, 128, [](float x, float y, float s) {
        float c = s * 0.5f, rMid = s * 0.5f - 7.f, half = 4.f;
        float d = std::sqrt((x - c) * (x - c) + (y - c) * (y - c));
        return half - std::fabs(d - rMid) + 0.5f;
    });
    setShape(m_glow, 128, [](float x, float y, float s) {
        float c = s * 0.5f, r = s * 0.5f - 1.f;
        float d = std::sqrt((x - c) * (x - c) + (y - c) * (y - c));
        float t = 1.f - Clampf(d / r, 0.f, 1.f);
        return t * t;
    });

    GdiFlush();

    // Collapse the DIB's blue channel into an R8 coverage texture.
    std::vector<uint8_t> r8((size_t)m_atlasW * m_atlasH);
    for (size_t i = 0, n = r8.size(); i < n; ++i) r8[i] = (uint8_t)(pix[i] & 0xFF);

    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);

    D3D11_TEXTURE2D_DESC td{};
    td.Width = (UINT)m_atlasW;
    td.Height = (UINT)m_atlasH;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA srd{};
    srd.pSysMem = r8.data();
    srd.SysMemPitch = (UINT)m_atlasW;
    if (FAILED(m_dev->CreateTexture2D(&td, &srd, &m_atlas))) return false;
    if (FAILED(m_dev->CreateShaderResourceView(m_atlas, nullptr, &m_atlasSrv))) return false;
    return true;
}

// ---------------------------------------------------------------- frame driver

void Renderer::WaitForPresentSlot()
{
    if (m_waitable) WaitForSingleObjectEx(m_waitable, 200, TRUE);
}

void Renderer::BeginFrame(Color clear)
{
    m_verts.clear();
    m_clipped = false;

    D3D11_VIEWPORT vp{ 0, 0, (float)m_w, (float)m_h, 0, 1 };
    m_ctx->RSSetViewports(1, &vp);
    ClearClip();

    m_ctx->OMSetRenderTargets(1, &m_rtv, nullptr);
    const float c[4] = { clear.r, clear.g, clear.b, clear.a };
    if (m_rtv) m_ctx->ClearRenderTargetView(m_rtv, c);

    D3D11_MAPPED_SUBRESOURCE ms{};
    if (SUCCEEDED(m_ctx->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)))
    {
        float* f = (float*)ms.pData;
        f[0] = 1.f / (float)m_w; f[1] = 1.f / (float)m_h; f[2] = 0; f[3] = 0;
        m_ctx->Unmap(m_cb, 0);
    }

    const UINT stride = sizeof(Vtx), off = 0;
    m_ctx->IASetInputLayout(m_il);
    m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_ctx->IASetVertexBuffers(0, 1, &m_vb, &stride, &off);
    m_ctx->VSSetShader(m_vs, nullptr, 0);
    m_ctx->VSSetConstantBuffers(0, 1, &m_cb);
    m_ctx->PSSetShader(m_ps, nullptr, 0);
    m_ctx->PSSetShaderResources(0, 1, &m_atlasSrv);
    m_ctx->PSSetSamplers(0, 1, &m_samp);
    const float bf[4] = { 0, 0, 0, 0 };
    m_ctx->OMSetBlendState(m_blend, bf, 0xFFFFFFFF);
    m_ctx->RSSetState(m_rs);
}

void Renderer::EndFrame() { Flush(); }

void Renderer::Present(bool vsync)
{
    UINT flags = 0;
    m_lastTorn = false;
    if (!vsync && m_tearing) { flags = DXGI_PRESENT_ALLOW_TEARING; m_lastTorn = true; }
    HRESULT hr = m_sc->Present(vsync ? 1 : 0, flags);
    m_occluded = (hr == DXGI_STATUS_OCCLUDED);
    // Refresh counts only mean something when presents are tied to vblank;
    // in immediate mode the difference is just the free-running frame surplus.
    if (vsync) UpdatePresentStats();
    else { m_haveLastStats = false; m_statsValid = false; }
}

void Renderer::UpdatePresentStats()
{
    DXGI_FRAME_STATISTICS fs{};
    if (m_sc && SUCCEEDED(m_sc->GetFrameStatistics(&fs)))
    {
        m_statsValid = true;
        m_presentCount = fs.PresentCount;
        if (m_haveLastStats)
        {
            int64_t dp = (int64_t)fs.PresentCount - (int64_t)m_lastPresentCount;
            int64_t dr = (int64_t)fs.PresentRefreshCount - (int64_t)m_lastRefreshCount;
            if (dp > 0 && dr > dp) m_dropped += dr - dp;
        }
        m_lastPresentCount = fs.PresentCount;
        m_lastRefreshCount = fs.PresentRefreshCount;
        m_haveLastStats = true;
    }
    else m_statsValid = false;
}

void Renderer::Flush()
{
    if (m_verts.empty()) return;
    size_t total = m_verts.size();
    for (size_t base = 0; base < total; base += kVbCap)
    {
        size_t n = std::min(kVbCap, total - base);
        D3D11_MAPPED_SUBRESOURCE ms{};
        if (FAILED(m_ctx->Map(m_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) break;
        memcpy(ms.pData, m_verts.data() + base, n * sizeof(Vtx));
        m_ctx->Unmap(m_vb, 0);
        m_ctx->Draw((UINT)n, 0);
    }
    m_verts.clear();
}

// ------------------------------------------------------------------- clipping

void Renderer::SetClip(const Rect2& r)
{
    Flush();
    m_clip = r;
    m_clipped = true;
    D3D11_RECT sr{ (LONG)std::floor(r.x), (LONG)std::floor(r.y),
                   (LONG)std::ceil(r.r()), (LONG)std::ceil(r.b()) };
    sr.left = std::max<LONG>(0, sr.left);
    sr.top = std::max<LONG>(0, sr.top);
    sr.right = std::min<LONG>(m_w, sr.right);
    sr.bottom = std::min<LONG>(m_h, sr.bottom);
    if (sr.right < sr.left) sr.right = sr.left;
    if (sr.bottom < sr.top) sr.bottom = sr.top;
    m_ctx->RSSetScissorRects(1, &sr);
}

void Renderer::ClearClip()
{
    Flush();
    m_clipped = false;
    D3D11_RECT sr{ 0, 0, (LONG)m_w, (LONG)m_h };
    m_ctx->RSSetScissorRects(1, &sr);
}

// ------------------------------------------------------------------ primitives

void Renderer::Quad(float x0, float y0, float x1, float y1,
                    float u0, float v0, float u1, float v1, Color c)
{
    if (c.a <= 0.f) return;
    const Vtx a{ x0, y0, u0, v0, c.r, c.g, c.b, c.a };
    const Vtx b{ x1, y0, u1, v0, c.r, c.g, c.b, c.a };
    const Vtx d{ x1, y1, u1, v1, c.r, c.g, c.b, c.a };
    const Vtx e{ x0, y1, u0, v1, c.r, c.g, c.b, c.a };
    m_verts.push_back(a); m_verts.push_back(b); m_verts.push_back(d);
    m_verts.push_back(a); m_verts.push_back(d); m_verts.push_back(e);
}

void Renderer::FillRect(const Rect2& r, Color c)
{
    if (r.w <= 0 || r.h <= 0) return;
    // Sample the middle of the solid block so the whole quad reads as opaque.
    float u = (m_white.u0 + m_white.u1) * 0.5f, v = (m_white.v0 + m_white.v1) * 0.5f;
    Quad(r.x, r.y, r.r(), r.b(), u, v, u, v, c);
}

void Renderer::FrameRect(const Rect2& r, float t, Color c)
{
    FillRect(Rect2{ r.x, r.y, r.w, t }, c);
    FillRect(Rect2{ r.x, r.b() - t, r.w, t }, c);
    FillRect(Rect2{ r.x, r.y + t, t, r.h - 2 * t }, c);
    FillRect(Rect2{ r.r() - t, r.y + t, t, r.h - 2 * t }, c);
}

void Renderer::Line(float x0, float y0, float x1, float y1, float t, Color c)
{
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) { FillRect(Rect2{ x0 - t * .5f, y0 - t * .5f, t, t }, c); return; }
    float nx = -dy / len * t * 0.5f, ny = dx / len * t * 0.5f;
    float u = (m_white.u0 + m_white.u1) * 0.5f, v = (m_white.v0 + m_white.v1) * 0.5f;
    const Vtx a{ x0 + nx, y0 + ny, u, v, c.r, c.g, c.b, c.a };
    const Vtx b{ x1 + nx, y1 + ny, u, v, c.r, c.g, c.b, c.a };
    const Vtx d{ x1 - nx, y1 - ny, u, v, c.r, c.g, c.b, c.a };
    const Vtx e{ x0 - nx, y0 - ny, u, v, c.r, c.g, c.b, c.a };
    m_verts.push_back(a); m_verts.push_back(b); m_verts.push_back(d);
    m_verts.push_back(a); m_verts.push_back(d); m_verts.push_back(e);
}

void Renderer::Disc(float cx, float cy, float rad, Color c)
{
    Quad(cx - rad, cy - rad, cx + rad, cy + rad,
         m_disc.u0, m_disc.v0, m_disc.u1, m_disc.v1, c);
}
void Renderer::Glow(float cx, float cy, float rad, Color c)
{
    Quad(cx - rad, cy - rad, cx + rad, cy + rad,
         m_glow.u0, m_glow.v0, m_glow.u1, m_glow.v1, c);
}
void Renderer::RingShape(float cx, float cy, float rad, Color c)
{
    Quad(cx - rad, cy - rad, cx + rad, cy + rad,
         m_ring.u0, m_ring.v0, m_ring.u1, m_ring.v1, c);
}
void Renderer::Cross(float cx, float cy, float size, float t, Color c)
{
    FillRect(Rect2{ cx - size, cy - t * .5f, size * 2, t }, c);
    FillRect(Rect2{ cx - t * .5f, cy - size, t, size * 2 }, c);
}

// ------------------------------------------------------------------------ text

void Renderer::TexQuad(const Rect2& dst, const Glyph& g, Color c)
{
    if (g.w <= 0) return;
    Quad(dst.x, dst.y, dst.r(), dst.b(), g.u0, g.v0, g.u1, g.v1, c);
}

float Renderer::TextW(int f, const char* s) const
{
    const FontInfo& fi = m_fonts[f];
    float w = 0;
    for (; *s; ++s)
    {
        int ci = (unsigned char)*s - 32;
        w += (ci >= 0 && ci < 95) ? fi.g[ci].adv : fi.advance;
    }
    return w;
}

void Renderer::Text(int f, float x, float y, Color c, const char* s)
{
    const FontInfo& fi = m_fonts[f];
    float px = std::round(x), py = std::round(y);
    for (; *s; ++s)
    {
        unsigned char ch = (unsigned char)*s;
        int ci = (int)ch - 32;
        if (ci < 0 || ci >= 95) { px += fi.advance; continue; }
        const Glyph& g = fi.g[ci];
        if (g.w > 0 && ch != ' ')
            TexQuad(Rect2{ px, py, g.w, g.h }, g, c);
        px += g.adv;
    }
}

void Renderer::Textf(int f, float x, float y, Color c, const char* fmt, ...)
{
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    Text(f, x, y, c, buf);
}

void Renderer::TextRight(int f, float xr, float y, Color c, const char* fmt, ...)
{
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    Text(f, xr - TextW(f, buf), y, c, buf);
}

void Renderer::TextCenter(int f, float cx, float y, Color c, const char* fmt, ...)
{
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    Text(f, cx - TextW(f, buf) * 0.5f, y, c, buf);
}
