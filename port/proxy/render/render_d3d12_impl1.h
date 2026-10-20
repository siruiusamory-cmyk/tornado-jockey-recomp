
namespace render {

inline unsigned& d3d12_fail_step() {
    static unsigned step = 0;
    return step;
}

inline FILE*& log_sink_slot() {
    static FILE* file = nullptr;
    return file;
}

inline bool& debug_flag() {
    static bool on = false;
    return on;
}

inline bool& debug_draws() {
    static bool on = true;
    return on;
}

inline bool& debug_layer_on() {
    static bool on = false;
    return on;
}

inline void log_line(const char* format, ...) {
    FILE* file = log_sink_slot();
    if (!file) return;
    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fflush(file);
}

inline void set_name(ID3D12Object* object, const char* text) {
    if (!object) return;
    wchar_t wide[96] = {};
    for (unsigned i = 0; text[i] && i < 95; ++i) wide[i] = (wchar_t)(unsigned char)text[i];
    object->SetName(wide);
}

inline HRESULT hr_note(const char* where, HRESULT hr) {
    if (debug_flag() && FAILED(hr)) log_line("# d3d12 fail %s hr=%08X\n", where, (unsigned)hr);
    return hr;
}

inline void flush_debug_messages(ID3D12Device* dev, const char* where) {
    if (!dev || !debug_flag()) return;
    ID3D12InfoQueue* queue = nullptr;
    if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&queue))) || !queue) return;
    UINT64 count = queue->GetNumStoredMessages();
    for (UINT64 i = 0; i < count; ++i) {
        SIZE_T size = 0;
        if (FAILED(queue->GetMessage(i, nullptr, &size)) || !size) continue;
        D3D12_MESSAGE* message = (D3D12_MESSAGE*)std::malloc(size);
        if (message && SUCCEEDED(queue->GetMessage(i, message, &size))) {
            log_line("# d3d12 debug %s sev=%d id=%d \"%s\"\n", where, (int)message->Severity,
                     (int)message->ID, (const char*)message->pDescription);
        }
        std::free(message);
    }
    queue->ClearStoredMessages();
    queue->Release();
}

inline D3D12_HEAP_PROPERTIES& heap12(D3D12_HEAP_TYPE type) {
    static D3D12_HEAP_PROPERTIES p[4] = {};
    p[type].Type = type;
    p[type].CreationNodeMask = 1;
    p[type].VisibleNodeMask = 1;
    return p[type];
}

inline D3D12_RESOURCE_DESC tex_desc(DWORD width, DWORD height, unsigned levels, unsigned faces,
                                    DXGI_FORMAT format) {
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = width;
    d.Height = height;
    d.DepthOrArraySize = (UINT16)faces;
    d.MipLevels = (UINT16)levels;
    d.Format = format;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    return d;
}

inline DXGI_FORMAT dxgi_format(DWORD format) {
    switch (format) {
        case kFmtA8R8G8B8:
        case kFmtX8R8G8B8: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case kFmtDXT1: return DXGI_FORMAT_BC1_UNORM;
        case kFmtDXT3: return DXGI_FORMAT_BC2_UNORM;
        case kFmtDXT5: return DXGI_FORMAT_BC3_UNORM;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

inline unsigned char* allocate_level(DWORD format, unsigned width, unsigned height) {
    size_t bytes = level_bytes(format, width, height);
    if (!bytes) bytes = 4;

    return new unsigned char[bytes + 16]();
}

inline void check_level_tail(Texture12* texture, unsigned face, unsigned level, const char* where) {
    if (!texture) return;
    unsigned lw = texture->width >> level ? texture->width >> level : 1;
    unsigned lh = texture->height >> level ? texture->height >> level : 1;
    size_t bytes = level_bytes(texture->format, lw, lh);
    if (!bytes) bytes = 4;
    const unsigned char* data = texture->data[face][level];
    if (!data) return;
    for (unsigned i = 0; i < 16; ++i) {
        if (data[bytes + i]) {
            log_line("# d3d12 level overrun %s tex=%08X face=%u level=%u %ux%u fmt=%08X tail=%02X\n",
                     where, (unsigned)(uintptr_t)texture, face, level, lw, lh,
                     (unsigned)texture->format, (unsigned)data[bytes + i]);
            return;
        }
    }
}

inline HRESULT Surface12::GetDesc(void* desc) {
    if (!desc) return E_POINTER;
    unsigned* d = (unsigned*)desc;
    std::memset(d, 0, 32);
    if (debug_flag()) {
        log_line("# d3d12 surfacedesc surf=%08X owner=%08X back=%u pix=%08X %ux%u L%u fmt=%08X\n",
                 (unsigned)(uintptr_t)this, (unsigned)(uintptr_t)owner, (unsigned)backbuffer,
                 (unsigned)(uintptr_t)pixels, owner ? owner->width : width,
                 owner ? owner->height : height, level, owner ? owner->format : format);
    }
    if (backbuffer) {

        d[0] = device->backFormat;
        d[1] = 1;
        d[4] = device->width * device->height * 4;
        d[5] = 1;
        d[6] = device->width;
        d[7] = device->height;
        return S_OK;
    }
    if (owner) {
        unsigned lw = owner->width >> level ? owner->width >> level : 1;
        unsigned lh = owner->height >> level ? owner->height >> level : 1;
        d[0] = owner->format;
        d[1] = 1;
        d[2] = owner->usage;
        d[3] = owner->pool;
        d[4] = level_bytes(owner->format, lw, lh);
        d[5] = 1;
        d[6] = lw;
        d[7] = lh;
        return S_OK;
    }
    d[0] = format;
    d[1] = 1;
    d[3] = 2;
    d[4] = width * height * 4;
    d[5] = 1;
    d[6] = width;
    d[7] = height;
    return S_OK;
}

inline HRESULT Surface12::LockRect(void* locked, const RECT* rect, DWORD flags) {
    (void)flags;
    if (!locked) return E_POINTER;
    unsigned* l = (unsigned*)locked;
    unsigned char* base = nullptr;
    unsigned pitch = 0;
    if (owner) {
        base = owner->data[face][level];
        unsigned lw = owner->width >> level ? owner->width >> level : 1;
        pitch = row_bytes(owner->format, lw);
    } else if (pixels) {
        base = pixels;
        pitch = width * 4;
    } else {
        return hr_note("SurfaceLockRect", E_FAIL);
    }
    if (rect && base) {
        unsigned bpp = owner && (owner->format == kFmtDXT1 || owner->format == kFmtDXT3 ||
                                 owner->format == kFmtDXT5)
                           ? 4
                           : 4;
        base += (size_t)rect->top * pitch + (size_t)rect->left * bpp;
    }
    l[0] = pitch;
    l[1] = (unsigned)(uintptr_t)base;
    return S_OK;
}

inline HRESULT Surface12::UnlockRect() {
    if (owner) {
        check_level_tail(owner, face, level, "surface");
        owner->dirty = true;
    }
    return S_OK;
}

inline HRESULT Texture12::GetLevelDesc(UINT level, void* desc) {
    if (!desc) return E_POINTER;
    unsigned* d = (unsigned*)desc;
    std::memset(d, 0, 32);
    unsigned lw = width >> level ? width >> level : 1;
    unsigned lh = height >> level ? height >> level : 1;
    d[0] = format;
    d[1] = 1;
    d[2] = usage;
    d[3] = pool;
    d[4] = level_bytes(format, lw, lh);
    d[5] = 1;
    d[6] = lw;
    d[7] = lh;
    if (debug_flag()) {
        log_line("# d3d12 leveldesc tex=%08X lev=%u %ux%u fmt=%08X usage=%u pool=%u\n",
                 (unsigned)(uintptr_t)this, level, lw, lh, (unsigned)format, (unsigned)usage,
                 (unsigned)pool);
    }
    return S_OK;
}

inline HRESULT Texture12::GetSurface(UINT surfaceFace, UINT level, Surface** out) {
    if (!out || level >= levels || surfaceFace >= faces)
        return hr_note("GetSurface", E_INVALIDARG);
    Surface12* s = new Surface12;
    s->device = device;
    s->owner = this;
    s->face = surfaceFace;
    s->level = level;
    s->pixels = nullptr;
    s->width = 0;
    s->height = 0;
    s->format = 0;
    AddRef();

    s->refs = 2;
    *out = s;
    return S_OK;
}

inline HRESULT Texture12::LockRect(UINT level, void* locked, const RECT* rect, DWORD flags) {
    (void)flags;
    if (!locked || level >= levels) return hr_note("TextureLockRect", E_INVALIDARG);
    unsigned* l = (unsigned*)locked;
    unsigned lw = width >> level ? width >> level : 1;
    l[0] = row_bytes(format, lw);
    l[1] = (unsigned)(uintptr_t)data[0][level];
    return S_OK;
}

inline HRESULT Texture12::UnlockRect(UINT level) {
    check_level_tail(this, 0, level, "texture");
    dirty = true;
    return S_OK;
}

inline HRESULT Buffer12::GetDesc(void* desc) {
    if (!desc) return E_POINTER;
    unsigned* d = (unsigned*)desc;
    std::memset(d, 0, 24);
    d[0] = format;
    d[1] = index ? 3 : 2;
    d[2] = usage;
    d[3] = pool;
    d[4] = length;
    d[5] = fvf;
    return S_OK;
}

inline HRESULT Buffer12::Lock(UINT offset, UINT size, unsigned char** bits, DWORD flags) {
    (void)size;
    (void)flags;
    if (!bits || offset > length) return hr_note("BufferLock", E_INVALIDARG);
    *bits = data + offset;
    return S_OK;
}

inline bool Device12::init(HWND hwnd, unsigned w, unsigned h, DWORD format, DWORD behavior,
                           const unsigned* capsDwords) {
    if ((behavior & kCreateFpuPreserve) == 0) _controlfp(_PC_24, _MCW_PC);
    threadGuard.own((unsigned)GetCurrentThreadId());
    window = hwnd;
    width = w;
    height = h;
    backFormat = format;
    if (capsDwords) {
        for (unsigned i = 0; i < 52; ++i) caps[i] = capsDwords[i];
    }
    rs[kRsZEnable] = 1;
    rs[kRsZWrite] = 1;
    rs[kRsZFunc] = 4;
    rs[kRsCullMode] = 3;
    rs[kRsFillMode] = 3;
    rs[kRsAlphaFunc] = 8;
    rs[kRsSrcBlend] = 2;
    rs[kRsDestBlend] = 1;
    rs[kRsBlendOp] = 1;
    rs[kRsLighting] = 1;
    rs[kRsClipping] = 1;

    rs[kRsDiffuseSource] = 1;
    rs[kRsAmbientSource] = 0;
    rs[kRsSpecularSource] = 2;
    rs[kRsColorVertex] = 1;

    for (unsigned s = 0; s < kMaxStages; ++s) {
        tss[s][1] = s == 0 ? 4 : 1;
        tss[s][2] = 2;
        tss[s][3] = 1;
        tss[s][4] = 2;
        tss[s][5] = 2;
        tss[s][6] = 1;
    }
    for (unsigned i = 0; i < 256; ++i) {
        for (unsigned j = 0; j < 16; ++j) world[i][j] = (i % 5 == j % 5) ? 1.0f : 0.0f;
    }
    for (unsigned j = 0; j < 16; ++j) {
        view[j] = (j % 5 == 0) ? 1.0f : 0.0f;
        proj[j] = (j % 5 == 0) ? 1.0f : 0.0f;
    }
    material[0][0] = material[0][1] = material[0][2] = material[0][3] = 1.0f;
    material[1][0] = material[1][1] = material[1][2] = material[1][3] = 1.0f;
    viewport[2] = (float)w;
    viewport[3] = (float)h;
    viewport[5] = 1.0f;
    if (debug_flag()) {
        log_line("# d3d12 init viewport dev=%08X %f,%f,%f,%f,%f,%f\n", (unsigned)(uintptr_t)this,
                 viewport[0], viewport[1], viewport[2], viewport[3], viewport[4], viewport[5]);
    }

    char debugFlag[8] = {};
    DWORD debugLen = GetEnvironmentVariableA("TJ_D3D12_DEBUG", debugFlag, sizeof debugFlag);
    debug_flag() = debugLen > 0 && (debugFlag[0] == '1' || debugFlag[0] == '3' ||
                                    debugFlag[0] == '5' ||
                                    debugFlag[0] == '7' || debugFlag[0] == '8' ||
                                    debugFlag[0] == '9');
    debug_draws() = (debugLen == 0 || debugFlag[0] != '3') &&
                    GetEnvironmentVariableA("TJ_D3D12_QUIET", nullptr, 0) == 0;
    noBlend = debugLen > 0 && debugFlag[0] == '9';
    noCull = debugLen > 0 && debugFlag[0] == '8';
    forceAlpha = debugLen > 0 && debugFlag[0] == '7';
    forceTexture = debugLen > 0 && debugFlag[0] == '5';
    char rows[16] = {};
    if (GetEnvironmentVariableA("TJ_D3D12_ROWS", rows, sizeof rows) > 0) {
        rowPresent = (unsigned)std::strtoul(rows, nullptr, 10);
        debug_flag() = true;
    }
    char ids[MAX_PATH] = {};
    if (GetEnvironmentVariableA("TJ_DRAW_ID", ids, sizeof ids) > 0) {
        idEnabled = true;
        std::strncpy(idOut, ids, sizeof idOut - 1);
        idMap = new unsigned[(size_t)w * h]();
        idDepth = new float[(size_t)w * h];
        for (size_t i = 0; i < (size_t)w * h; ++i) idDepth[i] = 1.0f;
    }
    char noz[8] = {};
    noDepth = GetEnvironmentVariableA("TJ_D3D12_NOZ", noz, sizeof noz) > 0 && noz[0] == '1';
    char pairs[16] = {};
    if (GetEnvironmentVariableA("TJ_D3D12_PAIRS", pairs, sizeof pairs) > 0) {
        unsigned value = (unsigned)std::strtoul(pairs, nullptr, 10);
        if (value >= 1) pairCapacity = value < pair_capacity() ? value : pair_capacity();
        debug_flag() = true;
        log_line("# d3d12 pairs capacity=%u of %u\n", pairCapacity, pair_capacity());
    }
    if (noBlend || noCull || forceAlpha || forceTexture) debug_flag() = true;
    if (debug_flag()) {
        ID3D12Debug* debug = nullptr;
        HRESULT debugHr = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
        log_line("# d3d12 debug interface hr=%08X\n", (unsigned)debugHr);
        if (SUCCEEDED(debugHr) && debug) {
            debug->EnableDebugLayer();
            debug->Release();
            debug_layer_on() = true;
            log_line("# d3d12 debug layer on\n");
        }
    }

    IDXGIFactory2* factory = nullptr;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) { d3d12_fail_step() = 3; return false; }
    IDXGIAdapter1* chosen = nullptr;
    char adapterEnv[16] = {};
    if (GetEnvironmentVariableA("TJ_ADAPTER", adapterEnv, sizeof adapterEnv) > 0) {
        unsigned vendor = (unsigned)std::strtoul(adapterEnv, nullptr, 16);
        for (unsigned i = 0; factory->EnumAdapters1(i, &chosen) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc = {};
            chosen->GetDesc1(&desc);
            log_line("# d3d12 adapter index=%u vendor=%04X device=%04X luid=%08X%08X name=%ls\n",
                     i, desc.VendorId, desc.DeviceId, (unsigned)desc.AdapterLuid.HighPart,
                     desc.AdapterLuid.LowPart, desc.Description);
            if (desc.VendorId == vendor) break;
            chosen->Release();
            chosen = nullptr;
        }
        if (!chosen) {
            log_line("# d3d12 adapter vendor=%04X not found\n", vendor);
            return false;
        }
    }
    if (FAILED(D3D12CreateDevice(chosen, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) { d3d12_fail_step() = 1; return false; }
    if (chosen) chosen->Release();
    set_name(dev, "tj device");

    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) { d3d12_fail_step() = 2; return false; }

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = w;
    sd.Height = h;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    HRESULT hr = factory->CreateSwapChainForHwnd(queue, hwnd, &sd, nullptr, nullptr,
                                                 (IDXGISwapChain1**)&swap);
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    factory->Release();
    if (FAILED(hr) || !swap) { d3d12_fail_step() = 4; return false; }
    frameIndex = swap->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rh = {};
    rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rh.NumDescriptors = 128;
    if (FAILED(dev->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&rtvHeap)))) { d3d12_fail_step() = 5; return false; }
    rtvSize = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_DESCRIPTOR_HEAP_DESC dh = {};
    dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dh.NumDescriptors = 1;
    if (FAILED(dev->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&dsvHeap)))) { d3d12_fail_step() = 6; return false; }
    D3D12_DESCRIPTOR_HEAP_DESC sh = {};
    sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    sh.NumDescriptors = kDescriptors;
    sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&srvHeap)))) { d3d12_fail_step() = 7; return false; }
    srvSize = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 2;
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &range;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 0;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 1;

    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC noMip = sampler;
    noMip.ShaderRegister = 1;
    noMip.MaxLOD = 0.0f;
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {sampler, noMip};
    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters = 2;
    rsd.pParameters = params;
    rsd.NumStaticSamplers = 2;
    rsd.pStaticSamplers = samplers;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob* blob = nullptr;
    ID3DBlob* error = nullptr;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error))) {
        d3d12_fail_step() = 8;
        return false;
    }
    hr = dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                  IID_PPV_ARGS(&root));
    blob->Release();
    if (error) error->Release();
    if (FAILED(hr)) { d3d12_fail_step() = 9; return false; }

    if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           IID_PPV_ARGS(&alloc[0])))) {
        d3d12_fail_step() = 10;
        return false;
    }
    if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           IID_PPV_ARGS(&xferAlloc)))) {
        d3d12_fail_step() = 11;
        return false;
    }
    if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc[0], nullptr,
                                      IID_PPV_ARGS(&list)))) {
        d3d12_fail_step() = 12;
        return false;
    }
    list->Close();
    if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, xferAlloc, nullptr,
                                      IID_PPV_ARGS(&xfer)))) {
        d3d12_fail_step() = 13;
        return false;
    }
    xfer->Close();
    if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) { d3d12_fail_step() = 14; return false; }
    fenceEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);

    for (unsigned i = 0; i < 2; ++i) {
        if (FAILED(swap->GetBuffer(i, IID_PPV_ARGS(&back[i])))) { d3d12_fail_step() = 15; return false; }
        D3D12_RENDER_TARGET_VIEW_DESC rv = {};
        rv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        dev->CreateRenderTargetView(back[i], &rv, rtv_handle(i));
    }

    D3D12_RESOURCE_DESC dd = tex_desc(w, h, 1, 1, DXGI_FORMAT_D32_FLOAT);
    dd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;
    if (FAILED(dev->CreateCommittedResource(&heap12(D3D12_HEAP_TYPE_DEFAULT),
                                            D3D12_HEAP_FLAG_NONE, &dd,
                                            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                                            IID_PPV_ARGS(&depth)))) {
        d3d12_fail_step() = 16;
        return false;
    }
    D3D12_DEPTH_STENCIL_VIEW_DESC dv = {};
    dv.Format = DXGI_FORMAT_D32_FLOAT;
    dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dev->CreateDepthStencilView(depth, &dv, dsvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_RESOURCE_DESC ud = {};
    ud.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    ud.Width = uploadSize;
    ud.Height = 1;
    ud.DepthOrArraySize = 1;
    ud.MipLevels = 1;
    ud.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ud.SampleDesc.Count = 1;
    void* mapped = nullptr;
    if (FAILED(dev->CreateCommittedResource(&heap12(D3D12_HEAP_TYPE_UPLOAD),
                                            D3D12_HEAP_FLAG_NONE, &ud,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                            IID_PPV_ARGS(&upload)))) {
        d3d12_fail_step() = 17;
        return false;
    }
    upload->Map(0, nullptr, &mapped);
    uploadMap = (unsigned char*)mapped;

    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (UINT64)w * h * 4;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.SampleDesc.Count = 1;
    if (FAILED(dev->CreateCommittedResource(&heap12(D3D12_HEAP_TYPE_READBACK),
                                            D3D12_HEAP_FLAG_NONE, &rd,
                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                            IID_PPV_ARGS(&readback)))) {
        d3d12_fail_step() = 18;
        return false;
    }
    readback->Map(0, nullptr, &mapped);
    readbackMap = (unsigned char*)mapped;

    unsigned white = 0xFFFFFFFFu;
    D3D12_RESOURCE_DESC wd = {};
    wd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    wd.Width = 256;
    wd.Height = 1;
    wd.DepthOrArraySize = 1;
    wd.MipLevels = 1;
    wd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    wd.SampleDesc.Count = 1;
    void* whiteMap = nullptr;
    if (FAILED(dev->CreateCommittedResource(&heap12(D3D12_HEAP_TYPE_UPLOAD),
                                            D3D12_HEAP_FLAG_NONE, &wd,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                            IID_PPV_ARGS(&whiteBuffer)))) {
        d3d12_fail_step() = 19;
        return false;
    }
    whiteBuffer->Map(0, nullptr, &whiteMap);
    std::memcpy(whiteMap, &white, 4);
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Buffer.NumElements = 1;
    dev->CreateShaderResourceView(whiteBuffer, &sv, srv_handle(0));

    dev->CopyDescriptorsSimple(1, srv_handle(1), srv_handle(0),
                               D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    dev->CopyDescriptorsSimple(1, srv_handle(2), srv_handle(0),
                               D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    set_name(queue, "tj queue");

    set_name(rtvHeap, "tj rtv heap");
    set_name(dsvHeap, "tj dsv heap");
    set_name(srvHeap, "tj srv heap");
    set_name(root, "tj root signature");
    set_name(alloc[0], "tj frame allocator");
    set_name(xferAlloc, "tj transfer allocator");
    set_name(list, "tj frame command list");
    set_name(xfer, "tj transfer command list");
    set_name(fence, "tj fence");
    for (unsigned i = 0; i < 2; ++i) {
        char name[32] = {};
        std::snprintf(name, sizeof name, "tj back buffer %u", i);
        set_name(back[i], name);
    }
    set_name(depth, "tj depth");
    set_name(upload, "tj upload ring");
    set_name(readback, "tj readback");
    set_name(whiteBuffer, "tj white texel");
    flush_debug_messages(dev, "init");
    return true;
}

inline D3D12_CPU_DESCRIPTOR_HANDLE Device12::rtv_handle(unsigned index) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)index * rtvSize;
    return h;
}

inline D3D12_CPU_DESCRIPTOR_HANDLE Device12::srv_handle(unsigned index) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = srvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)index * srvSize;
    return h;
}

inline D3D12_GPU_DESCRIPTOR_HANDLE Device12::srv_gpu(unsigned index) {
    D3D12_GPU_DESCRIPTOR_HANDLE h = srvHeap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += (UINT64)index * srvSize;
    return h;
}

inline unsigned Device12::srv_alloc() {
    if (!own_thread("srv_alloc")) return 0;
    unsigned index = srvSlots.alloc();
    if (!index && srvSlots.exhausted == 1)
        log_line("# d3d12 srv exhausted capacity=%u\n", texture_capacity());
    return index;
}

inline void Device12::srv_free(unsigned index) {
    if (!own_thread("srv_free")) return;
    srvSlots.release(index);
}

inline bool Device12::wait_gpu(const char* where) {
    if (!queue || !fence) return true;
    ++fenceValue;
    queue->Signal(fence, fenceValue);
    UINT64 completed = fence->GetCompletedValue();
    if (completed < fenceValue) {
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, 5000);
        completed = fence->GetCompletedValue();
    }
    if (completed >= fenceValue) {
        pump_retired(completed);
        return true;
    }
    if (gpuTimeouts++ == 0 || (gpuTimeouts & 255) == 0) {
        log_line("# d3d12 gpu stall at=%s value=%llu completed=%llu timeouts=%u frame_failed\n",
                 where, (unsigned long long)fenceValue, (unsigned long long)completed, gpuTimeouts);
    }
    return false;
}

inline void Device12::retire(ID3D12Resource* resource, unsigned srv) {
    if (!resource && !srv) return;
    if (!own_thread("retire")) return;

    if (!listOpen && retireQueue.count == RetireQueue::kSlots) wait_gpu("retire");
    unsigned dropped =
        retireQueue.push(listOpen, fenceValue, (unsigned long long)(uintptr_t)resource, srv);
    if (dropped && (dropped == 1 || (dropped & 255) == 0))
        log_line("# d3d12 retire full dropped=%u srv=%u\n", dropped, srv);
}

inline void Device12::retire_at(ID3D12Resource* resource, unsigned srv, UINT64 at) {
    if (!resource && !srv) return;
    if (!own_thread("retire transfer")) return;
    unsigned dropped = retireQueue.push_fenced(at, (unsigned long long)(uintptr_t)resource, srv);
    if (dropped && (dropped == 1 || (dropped & 255) == 0))
        log_line("# d3d12 retire full dropped=%u srv=%u\n", dropped, srv);
}

inline void Device12::submit_pending(UINT64 value) {
    if (!own_thread("submit_pending")) return;
    listOpen = false;
    unsigned lost = retireQueue.bind(value);
    if (lost) log_line("# d3d12 retire pending lost=%u fence=%llu\n", lost,
                       (unsigned long long)value);
}

inline void Device12::pump_retired(UINT64 completed) {
    if (!own_thread("pump_retired")) return;
    RetireQueue::Entry ready[RetireQueue::kSlots];
    unsigned n = retireQueue.drain(completed, ready, RetireQueue::kSlots);
    for (unsigned i = 0; i < n; ++i) {
        if (ready[i].resource) ((ID3D12Resource*)(uintptr_t)ready[i].resource)->Release();
        srv_free(ready[i].srv);
    }
}

inline void Device12::drain_retired() {
    if (!own_thread("drain_retired")) return;
    bool complete = wait_gpu("shutdown");

    bool releasePending = RetireQueue::release_pending_on_complete(complete, xferPending);
    RetireQueue::Entry pending[RetireQueue::kSlots];
    unsigned n = retireQueue.drain_pending(releasePending, pending, RetireQueue::kSlots);
    for (unsigned i = 0; i < n; ++i) {
        if (pending[i].resource) ((ID3D12Resource*)(uintptr_t)pending[i].resource)->Release();
        srv_free(pending[i].srv);
    }
    if (!complete) pump_retired(fence ? fence->GetCompletedValue() : 0);
    unsigned kept = retireQueue.count + retireQueue.pendingCount;
    if (kept) log_line("# d3d12 retire shutdown kept=%u\n", kept);
}

inline void Device12::shutdown() { drain_retired(); }

inline bool Device12::own_thread(const char* where) {
    bool first = false;
    unsigned self = (unsigned)GetCurrentThreadId();
    if (threadGuard.enter(self, first)) return true;
    frameFailed = true;
    if (first)
        log_line("# d3d12 thread violation tid=%u owner=%u at=%s\n", self,
                 threadGuard.known_owner(), where);
    return false;
}

inline void Device12::upload_texture(Texture12* t) {
    if (!t || !t->gpuRes) return;
    if (xferPending) {

        if (!wait_gpu("upload pending")) {
            frameFailed = true;
            return;
        }
        xferPending = false;
    }
    if (debug_flag()) {
        unsigned nz = 0;
        const unsigned char* b = t->data[0][0];
        size_t n = (size_t)row_bytes(t->format, t->width) * level_rows(t->format, t->height);
        for (size_t i = 0; i < n && i < 262144; ++i) nz += b[i] != 0;
        log_line("# d3d12 upload tex=%08X %ux%u fmt=%08X lev=%u nonzero=%u of %u first=%02X%02X%02X%02X\n",
                 (unsigned)(uintptr_t)t, t->width, t->height, (unsigned)t->format, t->levels, nz,
                 (unsigned)(n > 262144 ? 262144 : n), b[0], b[1], b[2], b[3]);
    }
    D3D12_RESOURCE_DESC d = t->gpuRes->GetDesc();
    unsigned subs = t->levels * t->faces;
    if (subs > 96) return;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[96] = {};
    UINT rows[96] = {};
    UINT64 rowSize[96] = {};
    UINT64 total = 0;
    dev->GetCopyableFootprints(&d, 0, subs, 0, fp, rows, rowSize, &total);
    D3D12_RESOURCE_DESC sd = {};
    sd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    sd.Width = total;
    sd.Height = 1;
    sd.DepthOrArraySize = 1;
    sd.MipLevels = 1;
    sd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    sd.SampleDesc.Count = 1;
    if (debug_flag()) {
        log_line("# d3d12 upload2 tex=%08X subs=%u total=%llu pitch=%u rows=%u fmt=%u wh=%ux%u\n",
                 (unsigned)(uintptr_t)t, subs, (unsigned long long)total,
                 fp[0].Footprint.RowPitch, rows[0], (unsigned)d.Format, (unsigned)d.Width,
                 (unsigned)d.Height);
    }
    ID3D12Resource* staging = nullptr;
    void* mapped = nullptr;
    if (FAILED(dev->CreateCommittedResource(&heap12(D3D12_HEAP_TYPE_UPLOAD),
                                            D3D12_HEAP_FLAG_NONE, &sd,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                            IID_PPV_ARGS(&staging)))) {
        // D3D12 refuses a buffer desc without a sample count; SampleDesc.Count must be 1.
        hr_note("upload staging", E_FAIL);
        return;
    }
    if (debug_flag()) {
        log_line("# d3d12 upload2 tex=%08X subs=%u total=%llu pitch=%u rows=%u state=%u\n",
                 (unsigned)(uintptr_t)t, subs, (unsigned long long)total,
                 fp[0].Footprint.RowPitch, rows[0], (unsigned)t->state);
    }
    staging->Map(0, nullptr, &mapped);
    set_name(staging, "tj staging");
    std::memset(mapped, 0, (size_t)total);
    unsigned sub = 0;
    bool block = t->format == kFmtDXT1 || t->format == kFmtDXT3 || t->format == kFmtDXT5;
    for (unsigned face = 0; face < t->faces; ++face) {
        for (unsigned level = 0; level < t->levels; ++level, ++sub) {
            const unsigned char* src = t->data[face][level];
            if (!src) continue;
            unsigned lw = t->width >> level ? t->width >> level : 1;
            unsigned lh = t->height >> level ? t->height >> level : 1;
            unsigned bytes = row_bytes(t->format, lw);
            unsigned count = level_rows(t->format, lh);
            unsigned char* dst = (unsigned char*)mapped + fp[sub].Offset;
            for (unsigned r = 0; r < count; ++r) {
                const unsigned char* in = src + (size_t)r * bytes;
                unsigned char* out = dst + (size_t)r * fp[sub].Footprint.RowPitch;
                if (block) {
                    std::memcpy(out, in, bytes);
                } else {
                    // d3d8 X8R8G8B8/A8R8G8B8 is B, G, R, X in memory; the shader wants R, G, B, A
                    // and X8R8G8B8 samples with alpha 1.
                    for (unsigned x = 0; x < lw; ++x) {
                        out[x * 4 + 0] = in[x * 4 + 2];
                        out[x * 4 + 1] = in[x * 4 + 1];
                        out[x * 4 + 2] = in[x * 4 + 0];
                        out[x * 4 + 3] = t->format == kFmtX8R8G8B8 ? 0xFF : in[x * 4 + 3];
                    }
                }
            }
        }
    }
    staging->Unmap(0, nullptr);

    hr_note("upload allocator", xferAlloc->Reset());
    hr_note("upload list", xfer->Reset(xferAlloc, nullptr));
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = t->gpuRes;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = t->state;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    if (t->state != D3D12_RESOURCE_STATE_COPY_DEST) xfer->ResourceBarrier(1, &b);
    for (unsigned i = 0; i < sub; ++i) {
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = t->gpuRes;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = i;
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = staging;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = fp[i];
        xfer->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
    D3D12_RESOURCE_BARRIER b2 = {};
    b2.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b2.Transition.pResource = t->gpuRes;
    b2.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b2.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b2.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    xfer->ResourceBarrier(1, &b2);
    xfer->Close();
    ID3D12CommandList* lists[] = {xfer};
    queue->ExecuteCommandLists(1, lists);
    t->state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (!wait_gpu("upload")) {

        xferPending = true;
        frameFailed = true;
        retire_at(staging, 0, fenceValue);
        flush_debug_messages(dev, "upload");
        return;
    }
    flush_debug_messages(dev, "upload");
    staging->Release();
}

inline Texture12* make_texture(Device12* device, unsigned width, unsigned height, unsigned levels,
                               unsigned faces, DWORD usage, DWORD format, DWORD pool) {
    Texture12* t = new Texture12;
    t->device = device;
    t->width = width;
    t->height = height;
    t->levels = levels ? levels : 1;
    if (t->levels > 16) t->levels = 16;
    t->faces = faces ? faces : 1;
    t->usage = usage;
    t->format = format;
    t->pool = pool;
    t->dirty = true;
    for (unsigned face = 0; face < t->faces; ++face) {
        for (unsigned level = 0; level < t->levels; ++level) {
            unsigned lw = t->width >> level ? t->width >> level : 1;
            unsigned lh = t->height >> level ? t->height >> level : 1;
            t->data[face][level] = allocate_level(format, lw, lh);
        }
    }
    DXGI_FORMAT dx = dxgi_format(format);
    if (dx == DXGI_FORMAT_UNKNOWN || !device->dev) return t;
    D3D12_RESOURCE_DESC d = tex_desc(width, height, t->levels, t->faces, dx);
    if (usage & 1) d.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    HRESULT hr = device->dev->CreateCommittedResource(
        &heap12(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &d,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&t->gpuRes));
    if (FAILED(hr)) {
        hr_note("CreateTexture", hr);
        t->gpuRes = nullptr;
        return t;
    }
    t->state = D3D12_RESOURCE_STATE_COMMON;
    char name[64] = {};
    std::snprintf(name, sizeof name, "tj texture %ux%u lev=%u fmt=%08X", width, height, t->levels,
                  (unsigned)format);
    set_name(t->gpuRes, name);
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = dx;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (t->faces == 6) {
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        sv.TextureCube.MipLevels = t->levels;
    } else {
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = t->levels;
    }
    t->srv = device->srv_alloc();
    if (!t->srv) {

        t->gpuRes->Release();
        t->gpuRes = nullptr;
        return t;
    }
    device->dev->CreateShaderResourceView(t->gpuRes, &sv, device->srv_handle(t->srv));
    if (usage & 1) {
        t->rtv = device->nextRtv;
        device->nextRtv = device->nextRtv + 1 < 128 ? device->nextRtv + 1 : 2;
        D3D12_RENDER_TARGET_VIEW_DESC rv = {};
        rv.Format = dx;
        rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->dev->CreateRenderTargetView(t->gpuRes, &rv, device->rtv_handle(t->rtv));
    }
    flush_debug_messages(device->dev, "texture");
    return t;
}

}
