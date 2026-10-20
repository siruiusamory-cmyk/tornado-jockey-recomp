
namespace render {

inline void Device12::bind_texture(Texture12* t, unsigned stage, unsigned slot) {
    unsigned at = slot + stage;
    if (!t || !t->gpuRes) {
        if (t) ++noTexture;
        dev->CopyDescriptorsSimple(1, srv_handle(at), srv_handle(0),
                                   D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return;
    }
    ++binds;

    if (t->dirty && (t->usage & 1) == 0) {
        ++dirtyBinds;
        upload_texture(t);

        if (frameFailed) return;
        t->dirty = false;
        ++uploaded;
    }
    if (t->state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = t->gpuRes;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = t->state;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(1, &b);
        t->state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    dev->CopyDescriptorsSimple(1, srv_handle(at), srv_handle(t->srv),
                               D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

inline D3D12_BLEND blend12(DWORD blend) {
    switch (blend) {
        case 1: return D3D12_BLEND_ZERO;
        case 2: return D3D12_BLEND_ONE;
        case 3: return D3D12_BLEND_SRC_COLOR;
        case 4: return D3D12_BLEND_INV_SRC_COLOR;
        case 5: return D3D12_BLEND_SRC_ALPHA;
        case 6: return D3D12_BLEND_INV_SRC_ALPHA;
        case 7: return D3D12_BLEND_DEST_ALPHA;
        case 8: return D3D12_BLEND_INV_DEST_ALPHA;
        case 9: return D3D12_BLEND_DEST_COLOR;
        case 10: return D3D12_BLEND_INV_DEST_COLOR;
        case 11: return D3D12_BLEND_SRC_ALPHA_SAT;
        case 12: return D3D12_BLEND_INV_SRC_ALPHA;
        case 13: return D3D12_BLEND_SRC_ALPHA;
        default: return D3D12_BLEND_ONE;
    }
}

inline D3D12_BLEND alpha_blend12(DWORD blend) {
    switch (blend) {
        case 3: return D3D12_BLEND_SRC_ALPHA;
        case 4: return D3D12_BLEND_INV_SRC_ALPHA;
        case 9: return D3D12_BLEND_DEST_ALPHA;
        case 10: return D3D12_BLEND_INV_DEST_ALPHA;
        default: return blend12(blend);
    }
}

inline D3D12_COMPARISON_FUNC compare12(DWORD func) {
    switch (func) {
        case 1: return D3D12_COMPARISON_FUNC_NEVER;
        case 2: return D3D12_COMPARISON_FUNC_LESS;
        case 3: return D3D12_COMPARISON_FUNC_EQUAL;
        case 4: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case 5: return D3D12_COMPARISON_FUNC_GREATER;
        case 6: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case 7: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        default: return D3D12_COMPARISON_FUNC_ALWAYS;
    }
}

inline ID3D12PipelineState* Device12::pipeline() {
    DXGI_FORMAT rtv = targetTexture ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;

    uint64_t key = pso_key(targetTexture != nullptr, rs);
    for (unsigned i = 0; i < psoCount; ++i) {
        if (pso[i].key == key) return pso[i].state;
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},

        {"COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 52, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
    d.pRootSignature = root;
    d.VS.pShaderBytecode = kShaderVS;
    d.VS.BytecodeLength = kShaderVSSize;
    d.PS.pShaderBytecode = kShaderPS;
    d.PS.BytecodeLength = kShaderPSSize;
    d.InputLayout.pInputElementDescs = layout;
    d.InputLayout.NumElements = 6;
    d.RasterizerState.FillMode = rs[kRsFillMode] == 2 ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = (noCull || rs[kRsCullMode] == 1) ? D3D12_CULL_MODE_NONE
                                 : rs[kRsCullMode] == 2        ? D3D12_CULL_MODE_FRONT
                                                               : D3D12_CULL_MODE_BACK;
    d.RasterizerState.FrontCounterClockwise = FALSE;
    // D3DRS_CLIPPING FALSE skips z clipping and clamps depth; d3d8 default is TRUE.
    d.RasterizerState.DepthClipEnable = rs[kRsClipping] ? TRUE : FALSE;
    d.BlendState.RenderTarget[0].BlendEnable =
        (rs[kRsBlendEnable] && !noBlend) ? TRUE : FALSE;
    d.BlendState.RenderTarget[0].SrcBlend = blend12(rs[kRsSrcBlend]);
    d.BlendState.RenderTarget[0].DestBlend = blend12(rs[kRsDestBlend]);
    static const D3D12_BLEND_OP kBlendOps[5] = {
        D3D12_BLEND_OP_ADD, D3D12_BLEND_OP_SUBTRACT, D3D12_BLEND_OP_REV_SUBTRACT,
        D3D12_BLEND_OP_MIN, D3D12_BLEND_OP_MAX};
    const D3D12_BLEND_OP blendOp = kBlendOps[blend_op_index(rs[kRsBlendOp])];
    d.BlendState.RenderTarget[0].BlendOp = blendOp;
    d.BlendState.RenderTarget[0].SrcBlendAlpha = alpha_blend12(rs[kRsSrcBlend]);
    d.BlendState.RenderTarget[0].DestBlendAlpha = alpha_blend12(rs[kRsDestBlend]);

    d.BlendState.RenderTarget[0].BlendOpAlpha = blendOp;
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.DepthStencilState.DepthEnable = (rs[kRsZEnable] && !noDepth) ? TRUE : FALSE;

    d.DepthStencilState.DepthWriteMask =
        (rs[kRsZEnable] && rs[kRsZWrite] && !noDepth) ? D3D12_DEPTH_WRITE_MASK_ALL
                                          : D3D12_DEPTH_WRITE_MASK_ZERO;
    d.DepthStencilState.DepthFunc = compare12(rs[kRsZFunc]);
    d.DepthStencilState.StencilEnable = FALSE;
    d.SampleMask = 0xFFFFFFFFu;
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = rtv;
    d.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    d.SampleDesc.Count = 1;
    ID3D12PipelineState* state = nullptr;
    HRESULT psoHr = dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&state));
    if (FAILED(psoHr)) {
        if (debug_flag()) {
            log_line("# d3d12 pso fail hr=%08X rtv=%d z=%u zw=%u blend=%u/%u/%u/%u cull=%u fill=%u "
                     "atest=%u/af=%u/aref=%u clip=%u\n",
                     (unsigned)psoHr, (int)d.RTVFormats[0], (unsigned)rs[kRsZEnable],
                     (unsigned)rs[kRsZWrite], (unsigned)rs[kRsBlendEnable],
                     (unsigned)rs[kRsSrcBlend], (unsigned)rs[kRsDestBlend],
                     (unsigned)rs[kRsBlendOp], (unsigned)rs[kRsCullMode], (unsigned)rs[kRsFillMode],
                     (unsigned)rs[kRsAlphaTest], (unsigned)rs[kRsAlphaFunc],
                     (unsigned)rs[kRsAlphaRef], (unsigned)rs[kRsClipping]);
        }
        return nullptr;
    }
    if (psoCount < kPsoSlots) {
        pso[psoCount].key = key;
        pso[psoCount].state = state;
        ++psoCount;
    }
    return state;
}

inline void Device12::begin_target() {
    if (frameFailed) return;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
    if (targetTexture && targetTexture->rtv) {
        if (targetTexture->state != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            D3D12_RESOURCE_BARRIER b = {};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = targetTexture->gpuRes;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = targetTexture->state;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            list->ResourceBarrier(1, &b);
            targetTexture->state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }
        rtv = rtv_handle(targetTexture->rtv);
    } else {
        rtv = rtv_handle(frameIndex);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    D3D12_VIEWPORT vp = {};
    vp.TopLeftX = viewport[0];
    vp.TopLeftY = viewport[1];
    vp.Width = viewport[2];
    vp.Height = viewport[3];
    vp.MinDepth = viewport[4];
    vp.MaxDepth = viewport[5] == 0 ? 1.0f : viewport[5];
    list->RSSetViewports(1, &vp);
    D3D12_RECT scissor = {0, 0, (LONG)width, (LONG)height};
    list->RSSetScissorRects(1, &scissor);
    boundRtv = 1;
}

inline HRESULT Device12::BeginScene() {
    reset_frame();
    sceneOpen = true;
    return S_OK;
}

inline void Device12::reset_frame() {
    if (!list) return;
    static bool first = true;
    if (!first && !wait_gpu("reset")) {

        frameFailed = true;
        return;
    }
    frameFailed = false;
    first = false;
    xferPending = false;
    alloc[0]->Reset();
    list->Reset(alloc[0], nullptr);
    listOpen = true;
    ID3D12DescriptorHeap* heaps[] = {srvHeap};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(root);
    list->SetGraphicsRootDescriptorTable(0, srv_gpu(1));
    uploadUsed = 0;
    pairUsed = 0;
    boundRtv = 0;
    if (targetTexture) {
        D3D12_RESOURCE_STATES before = targetTexture->state;
        if (before != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            targetTexture->state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }
    }
    begin_target();
}

inline void Device12::flush_draws() {
    if (!list) return;
    list->Close();
    ID3D12CommandList* lists[] = {list};
    queue->ExecuteCommandLists(1, lists);

    submit_pending(fenceValue + 1);
    if (!wait_gpu("flush")) {

        frameFailed = true;
        return;
    }
    alloc[0]->Reset();
    list->Reset(alloc[0], nullptr);
    listOpen = true;
    ID3D12DescriptorHeap* heaps[] = {srvHeap};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(root);
    begin_target();
    pairUsed = 0;
    ++flushes;
}

inline HRESULT Device12::Present(const RECT* source, const RECT* dest, HWND w, const void* dirty) {
    (void)source;
    (void)dest;
    (void)w;
    (void)dirty;
    if (!list) return E_FAIL;
    if (idEnabled) {
        if (presents + 1 == rowPresent) {
            std::memset(idMap, 0, (size_t)width * height * sizeof(unsigned));
            for (size_t i = 0; i < (size_t)width * height; ++i) idDepth[i] = 1.0f;
            idDraw = 0;
        } else if (presents == rowPresent) {
            FILE* out = std::fopen(idOut, "wb");
            if (out) {
                std::fprintf(out, "TJID %u %u %u\n", width, height, idDraw);
                std::fwrite(idMap, sizeof(unsigned), (size_t)width * height, out);
                std::fclose(out);
                log_line("# d3d12 draw ids=%u out=%s\n", idDraw, idOut);
            }
        }
    }
    if (frameFailed) {

        ++presents;
        return E_FAIL;
    }
    if (targetTexture && targetTexture->state == D3D12_RESOURCE_STATE_RENDER_TARGET) {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = targetTexture->gpuRes;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(1, &b);
        targetTexture->state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    list->Close();
    ID3D12CommandList* lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    submit_pending(fenceValue + 1);
    if (!wait_gpu("present")) {

        frameFailed = true;
        ++presents;
        return E_FAIL;
    }
    flush_debug_messages(dev, "present");

    HRESULT hr = swap->Present(0, 0);
    frameIndex = swap->GetCurrentBackBufferIndex();
    ++presents;
    if (debug_flag() && presents % 100 == 0) {
        log_line("# d3d12 calls prim=%u indexed=%u up=%u drops=%u draws=%u skipped=%u stageOps=%u "
                 "binds=%u dirty=%u uploaded=%u missing=%u flush=%u srv=%u/%u\n",
                 callsPrimitive, callsIndexed, callsUp, drops, draws, skipped, stageOps, binds,
                 dirtyBinds, uploaded, noTexture, flushes, srvSlots.reused, srvSlots.exhausted);

        log_line("# d3d12 pstate stage1=%u projected=%u fog=%u factor=%u mod2x=%u modadd=%u "
                 "modarg=%u fogmode_unsupported=%u\n",
                 stage1Draws, projectedDraws, fogDraws, factorDraws, mod2xDraws, modAlphaAddDraws,
                 modArgDraws, fogUnsupported);
        for (unsigned i = 0; i < comboCount; ++i) {
            const DWORD* v = combos[i].v;
            log_line("# d3d12 combo draws=%u s0=%u/%u/%u a=%u/%u/%u s1=%u/%u/%u a=%u/%u/%u "
                     "tci0=%08X tff0=%X tci1=%08X tff1=%X\n",
                     combos[i].draws, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9],
                     v[10], v[11], v[12], v[13], v[14], v[15]);
        }
    }
    return hr;
}

inline HRESULT Device12::Clear(DWORD count, const RECT* rects, DWORD flags, DWORD color, float z,
                               DWORD stencil) {
    (void)count;
    (void)rects;
    (void)stencil;
    if (!list) return E_FAIL;
    if (frameFailed) return E_FAIL;
    if (flags & 1) {
        float rgba[4] = {((color >> 16) & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f,
                         (color & 0xFF) / 255.0f, ((color >> 24) & 0xFF) / 255.0f};
        D3D12_CPU_DESCRIPTOR_HANDLE rtv =
            targetTexture && targetTexture->rtv ? rtv_handle(targetTexture->rtv)
                                                : rtv_handle(frameIndex);
        list->ClearRenderTargetView(rtv, rgba, 0, nullptr);
    }
    if (flags & 2) {
        list->ClearDepthStencilView(dsvHeap->GetCPUDescriptorHandleForHeapStart(),
                                    D3D12_CLEAR_FLAG_DEPTH, z, 0, 0, nullptr);
    }
    return S_OK;
}

inline HRESULT Device12::SetTransform(unsigned state, const void* matrix) {
    if (!matrix) return E_POINTER;
    const float* m = (const float*)matrix;
    if (state == 2) {
        std::memcpy(view, m, 64);
    } else if (state == 3) {
        std::memcpy(proj, m, 64);
    } else if (state >= 256) {
        std::memcpy(world[state & 0xFF], m, 64);
    } else {
        std::memcpy(textureMatrix[state < 16 ? 0 : state - 16], m, 64);
    }
    xfDirty = true;
    return S_OK;
}

inline HRESULT Device12::GetTransform(unsigned state, void* matrix) {
    if (!matrix) return E_POINTER;
    if (state == 2) {
        std::memcpy(matrix, view, 64);
    } else if (state == 3) {
        std::memcpy(matrix, proj, 64);
    } else if (state >= 256) {
        std::memcpy(matrix, world[state & 0xFF], 64);
    } else {
        std::memcpy(matrix, textureMatrix[state < 16 ? 0 : state - 16], 64);
    }
    return S_OK;
}

inline HRESULT Device12::SetViewport(const void* vp) {
    if (!vp) return E_POINTER;
    const DWORD* d = (const DWORD*)vp;
    viewport[0] = (float)d[0];
    viewport[1] = (float)d[1];
    viewport[2] = (float)d[2];
    viewport[3] = (float)d[3];
    viewport[4] = ((const float*)vp)[4];
    viewport[5] = ((const float*)vp)[5];
    if (list) {
        D3D12_VIEWPORT v = {};
        v.TopLeftX = viewport[0];
        v.TopLeftY = viewport[1];
        v.Width = viewport[2];
        v.Height = viewport[3];
        v.MinDepth = viewport[4];
        v.MaxDepth = viewport[5] == 0 ? 1.0f : viewport[5];
        list->RSSetViewports(1, &v);
    }
    return S_OK;
}

inline HRESULT Device12::GetViewport(void* vp) {
    if (!vp) return E_POINTER;
    DWORD* d = (DWORD*)vp;
    d[0] = (DWORD)viewport[0];
    d[1] = (DWORD)viewport[1];
    d[2] = (DWORD)viewport[2];
    d[3] = (DWORD)viewport[3];
    ((float*)vp)[4] = viewport[4];
    ((float*)vp)[5] = viewport[5];
    return S_OK;
}

inline HRESULT Device12::SetMaterial(const void* m) {
    if (!m) return E_POINTER;
    const float* f = (const float*)m;
    std::memcpy(material[0], f, 16);
    std::memcpy(material[1], f + 4, 16);
    std::memcpy(material[2], f + 8, 16);
    std::memcpy(material[3], f + 12, 16);
    material[4][0] = f[16];
    return S_OK;
}

inline HRESULT Device12::SetLight(DWORD index, const void* light) {
    if (!light || index >= kMaxLights) return E_INVALIDARG;
    const unsigned* d = (const unsigned*)light;
    Light8& l = lights[index];
    l.type = d[0];
    const float* f = (const float*)light;
    l.diffuse[0] = f[1];
    l.diffuse[1] = f[2];
    l.diffuse[2] = f[3];
    l.diffuse[3] = f[4];
    l.specular[0] = f[5];
    l.specular[1] = f[6];
    l.specular[2] = f[7];
    l.specular[3] = f[8];
    l.ambient[0] = f[9];
    l.ambient[1] = f[10];
    l.ambient[2] = f[11];
    l.ambient[3] = f[12];
    l.position[0] = f[13];
    l.position[1] = f[14];
    l.position[2] = f[15];
    l.direction[0] = f[16];
    l.direction[1] = f[17];
    l.direction[2] = f[18];
    l.range = f[19];
    l.falloff = f[20];
    l.attenuation0 = f[21];
    l.attenuation1 = f[22];
    l.attenuation2 = f[23];
    l.theta = f[24];
    l.phi = f[25];
    return S_OK;
}

inline HRESULT Device12::LightEnable(DWORD index, BOOL enable) {
    if (index >= kMaxLights) return E_INVALIDARG;
    lightOn[index] = enable != 0;
    return S_OK;
}

inline HRESULT Device12::SetRenderState(unsigned state, DWORD value) {
    if (state < 256) rs[state] = value;
    return S_OK;
}

inline HRESULT Device12::GetRenderState(unsigned state, DWORD* value) {
    if (!value) return E_POINTER;
    *value = state < 256 ? rs[state] : 0;
    return S_OK;
}

inline HRESULT Device12::EndStateBlock(DWORD* token) {
    if (token) *token = 1;
    return S_OK;
}

inline HRESULT Device12::GetTextureStageState(DWORD stage, unsigned type, DWORD* value) {
    if (!value || stage >= kMaxStages || type >= 32) return E_INVALIDARG;
    *value = tss[stage][type];
    return S_OK;
}

inline HRESULT Device12::SetTextureStageState(DWORD stage, unsigned type, DWORD value) {
    if (stage < kMaxStages && type < 32) tss[stage][type] = value;
    return S_OK;
}

inline HRESULT Device12::ValidateDevice(DWORD* passes) {
    if (passes) *passes = 1;
    return S_OK;
}

inline HRESULT Device12::GetInfo(DWORD id, void* info, DWORD size) {
    (void)id;
    (void)info;
    (void)size;
    return E_NOTIMPL;
}

inline HRESULT Device12::GetDeviceCaps(void* out) {
    if (!out) return E_POINTER;
    std::memcpy(out, caps, sizeof caps);
    return S_OK;
}

inline HRESULT Device12::GetDisplayMode(void* mode) {
    if (!mode) return E_POINTER;
    unsigned* m = (unsigned*)mode;
    m[0] = width;
    m[1] = height;
    m[2] = 60;
    m[3] = kFmtX8R8G8B8;
    return S_OK;
}

inline HRESULT Device12::GetDirect3D(void** out) {
    if (!out) return E_POINTER;
    void* root = root_com();
    if (!root) {
        *out = nullptr;
        return hr_note("GetDirect3D", E_FAIL);
    }
    ((ULONG(__stdcall*)(void*))(*(void***)root)[1])(root);
    *out = root;
    return S_OK;
}

inline HRESULT Device12::Reset(void* params) {
    (void)params;
    if (list && !wait_gpu("Reset")) return E_FAIL;
    return S_OK;
}

inline HRESULT Device12::SetVertexShader(DWORD handle) {
    fvf = handle;
    return S_OK;
}

inline HRESULT Device12::SetTexture(DWORD stage, Texture* texture) {
    if (stage >= kMaxStages) return S_OK;
    Texture12* t = static_cast<Texture12*>(texture);
    if (t) t->AddRef();
    if (textures[stage]) textures[stage]->Release();
    textures[stage] = t;
    return S_OK;
}

inline HRESULT Device12::SetStreamSource(UINT streamIndex, Buffer* buffer, UINT stride) {
    if (streamIndex != 0) return S_OK;
    Buffer12* b = static_cast<Buffer12*>(buffer);
    if (b) b->AddRef();
    if (stream) stream->Release();
    stream = b;
    streamStride = stride;
    return S_OK;
}

inline HRESULT Device12::SetIndices(Buffer* buffer, UINT base) {
    Buffer12* b = static_cast<Buffer12*>(buffer);
    if (b) b->AddRef();
    if (indices) indices->Release();
    indices = b;
    baseVertex = base;
    return S_OK;
}

inline HRESULT Device12::GetBackBuffer(UINT index, unsigned type, Surface** out) {
    (void)index;
    (void)type;
    if (!out) return E_POINTER;
    Surface12* s = new Surface12;
    s->device = this;
    s->owner = nullptr;
    s->pixels = nullptr;
    s->backbuffer = true;
    s->width = width;
    s->height = height;
    s->format = backFormat;
    *out = s;
    return S_OK;
}

inline HRESULT Device12::GetRenderTarget(Surface** out) {
    if (!targetTexture) return GetBackBuffer(0, 0, out);
    if (!out) return E_POINTER;
    Surface** surface = out;
    return targetTexture->GetSurface(0, 0, surface);
}

inline HRESULT Device12::GetDepthStencilSurface(Surface** out) {
    if (!out) return E_POINTER;
    Surface12* s = new Surface12;
    s->device = this;
    s->owner = nullptr;
    s->pixels = nullptr;
    s->width = width;
    s->height = height;
    s->format = kFmtD16;
    *out = s;
    return S_OK;
}

inline HRESULT Device12::CreateTexture(UINT w, UINT h, UINT levels, DWORD usage, DWORD format,
                                       DWORD pool, Texture** out) {
    if (!out) return E_POINTER;
    *out = make_texture(this, w, h, levels, 1, usage, format, pool);
    if (debug_flag() && *out) {
        log_line("# d3d12 createtexture tex=%08X %ux%u lev=%u usage=%u fmt=%08X pool=%u gpu=%u\n",
                 (unsigned)(uintptr_t)*out, w, h, levels, (unsigned)usage, (unsigned)format,
                 (unsigned)pool, (*(Texture12**)out)->gpuRes != nullptr);
    }
    return S_OK;
}

inline HRESULT Device12::CreateCubeTexture(UINT edge, UINT levels, DWORD usage, DWORD format,
                                           DWORD pool, Texture** out) {
    if (!out) return E_POINTER;
    *out = make_texture(this, edge, edge, levels, 6, usage, format, pool);
    return S_OK;
}

inline HRESULT Device12::CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, DWORD pool,
                                            Buffer** out) {
    if (!out) return E_POINTER;
    Buffer12* b = new Buffer12;
    b->data = new unsigned char[length ? length : 4]();
    b->length = length;
    b->usage = usage;
    b->fvf = fvf;
    b->pool = pool;
    b->format = fvf;
    b->index = false;
    *out = b;
    return S_OK;
}

inline HRESULT Device12::CreateIndexBuffer(UINT length, DWORD usage, DWORD format, DWORD pool,
                                           Buffer** out) {
    if (!out) return E_POINTER;
    Buffer12* b = new Buffer12;
    b->data = new unsigned char[length ? length : 4]();
    b->length = length;
    b->usage = usage;
    b->fvf = 0;
    b->pool = pool;
    b->format = format;
    b->index = true;
    *out = b;
    return S_OK;
}

inline HRESULT Device12::CreateDepthStencilSurface(UINT w, UINT h, DWORD format, unsigned ms,
                                                   Surface** out) {
    (void)w;
    (void)h;
    (void)format;
    (void)ms;
    return GetDepthStencilSurface(out);
}

inline HRESULT Device12::CreateImageSurface(UINT w, UINT h, DWORD format, Surface** out) {
    if (!out) return E_POINTER;
    Surface12* s = new Surface12;
    s->device = this;
    s->owner = nullptr;
    s->pixels = new unsigned char[(size_t)w * h * 4]();
    s->width = w;
    s->height = h;
    s->format = format;
    *out = s;
    return S_OK;
}

inline HRESULT Device12::UpdateTexture(Texture* source, Texture* dest) {
    Texture12* s = static_cast<Texture12*>(source);
    Texture12* d = static_cast<Texture12*>(dest);
    if (!s || !d) return E_INVALIDARG;
    for (unsigned face = 0; face < d->faces && face < s->faces; ++face) {
        for (unsigned level = 0; level < d->levels && level < s->levels; ++level) {
            unsigned lw = d->width >> level ? d->width >> level : 1;
            unsigned lh = d->height >> level ? d->height >> level : 1;
            size_t bytes = level_bytes(d->format, lw, lh);
            if (s->data[face][level] && d->data[face][level]) {
                std::memcpy(d->data[face][level], s->data[face][level], bytes);
            }
        }
    }
    d->dirty = true;
    return S_OK;
}

inline HRESULT Device12::CopyRects(Surface* source, const RECT* rects, UINT count, Surface* dest,
                                   const POINT* points) {
    Surface12* s = static_cast<Surface12*>(source);
    Surface12* d = static_cast<Surface12*>(dest);
    if (!s || !d) return hr_note("CopyRects", E_INVALIDARG);
    if (frameFailed) return E_FAIL;
    if (debug_flag()) {

        const RECT& r0 = rects && count ? rects[0] : RECT{0, 0, 0, 0};
        log_line("# d3d12 copyrects src=%08X owner=%08X back=%u pix=%08X %ux%u L%u count=%u "
                 "rect=%ld,%ld,%ld,%ld dst=%08X owner=%08X back=%u pix=%08X %ux%u L%u "
                 "point=%ld,%ld\n",
                 (unsigned)(uintptr_t)s, (unsigned)(uintptr_t)s->owner, (unsigned)s->backbuffer,
                 (unsigned)(uintptr_t)s->pixels, s->owner ? s->owner->width : s->width,
                 s->owner ? s->owner->height : s->height, s->level, count, r0.left, r0.top,
                 r0.right, r0.bottom, (unsigned)(uintptr_t)d, (unsigned)(uintptr_t)d->owner,
                 (unsigned)d->backbuffer, (unsigned)(uintptr_t)d->pixels,
                 d->owner ? d->owner->width : d->width,
                 d->owner ? d->owner->height : d->height, d->level, points ? points[0].x : 0,
                 points ? points[0].y : 0);
        log_line("# d3d12 copyformats src=%08X dst=%08X\n",
                 (unsigned)(s->owner ? s->owner->format : s->format),
                 (unsigned)(d->owner ? d->owner->format : d->format));
    }
    if (!s->backbuffer) {

        unsigned char* srcBits = s->owner ? s->owner->data[s->face][s->level] : s->pixels;
        unsigned char* dstBits = d->owner ? d->owner->data[d->face][d->level] : d->pixels;
        if (!srcBits || !dstBits) return hr_note("CopyRects", E_INVALIDARG);
        unsigned srcW = s->owner ? s->owner->width >> s->level : s->width;
        unsigned srcH = s->owner ? s->owner->height >> s->level : s->height;
        unsigned dstW = d->owner ? d->owner->width >> d->level : d->width;
        unsigned dstH = d->owner ? d->owner->height >> d->level : d->height;
        DWORD srcFormat = s->owner ? s->owner->format : s->format;
        DWORD dstFormat = d->owner ? d->owner->format : d->format;

        if (srcFormat != dstFormat) return hr_note("CopyRects", (HRESULT)0x8876086CL);
        bool block = srcFormat == kFmtDXT1 || srcFormat == kFmtDXT3 || srcFormat == kFmtDXT5;
        unsigned copies = count ? count : 1;
        for (unsigned i = 0; i < copies; ++i) {
            RECT region = rects ? rects[i] : RECT{0, 0, (LONG)srcW, (LONG)srcH};
            POINT at = points ? points[i] : POINT{0, 0};

            if (region.left < 0 || region.top < 0 || region.right > (LONG)srcW ||
                region.bottom > (LONG)srcH || at.x < 0 || at.y < 0 ||
                at.x + (region.right - region.left) > (LONG)dstW ||
                at.y + (region.bottom - region.top) > (LONG)dstH) {
                return hr_note("CopyRects", (HRESULT)0x8876086CL);
            }
            unsigned columns = region.right - region.left;
            unsigned rows = region.bottom - region.top;
            if (block) rows = (rows + 3) / 4;
            unsigned srcPitch = row_bytes(srcFormat, srcW);
            unsigned dstPitch = row_bytes(dstFormat, dstW);
            unsigned bytes = row_bytes(srcFormat, columns);
            for (unsigned y = 0; y < rows; ++y) {
                std::memcpy(dstBits + (size_t)(at.y + y) * dstPitch + (size_t)at.x * 4,
                            srcBits + (size_t)(region.top + y) * srcPitch +
                                (size_t)region.left * 4,
                            bytes);
            }
        }
        if (d->owner) d->owner->dirty = true;
        return S_OK;
    }
    if (!d->pixels) return E_INVALIDARG;
    if (!list) return E_FAIL;
    D3D12_RESOURCE_BARRIER toCopy = {};
    toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopy.Transition.pResource = back[frameIndex];
    toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &toCopy);
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = readback;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Offset = 0;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    dst.PlacedFootprint.Footprint.Width = width;
    dst.PlacedFootprint.Footprint.Height = height;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = width * 4;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = back[frameIndex];
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER back2 = {};
    back2.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    back2.Transition.pResource = back[frameIndex];
    back2.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    back2.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    back2.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    list->ResourceBarrier(1, &back2);
    list->Close();
    ID3D12CommandList* lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    submit_pending(fenceValue + 1);
    if (!wait_gpu("copyrects")) {

        frameFailed = true;
        return E_FAIL;
    }
    flush_debug_messages(dev, "copyrects");

    unsigned readRows = height < d->height ? height : d->height;
    unsigned readBytes = (width < d->width ? width : d->width) * 4;
    for (unsigned y = 0; y < readRows; ++y) {
        std::memcpy(d->pixels + (size_t)y * d->width * 4, readbackMap + (size_t)y * width * 4,
                    readBytes);
    }

    alloc[0]->Reset();
    list->Reset(alloc[0], nullptr);
    listOpen = true;
    ID3D12DescriptorHeap* heaps[] = {srvHeap};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(root);
    list->SetGraphicsRootDescriptorTable(0, srv_gpu(1));
    begin_target();
    return S_OK;
}

inline HRESULT Device12::SetRenderTarget(Surface* target, Surface* depthTarget) {
    (void)depthTarget;
    Surface12* s = static_cast<Surface12*>(target);
    targetTexture = (s && s->owner) ? s->owner : nullptr;

    viewport[0] = 0.0f;
    viewport[1] = 0.0f;
    viewport[2] = (float)(targetTexture ? targetTexture->width : width);
    viewport[3] = (float)(targetTexture ? targetTexture->height : height);
    viewport[4] = 0.0f;
    viewport[5] = 1.0f;
    if (list && !frameFailed) begin_target();
    return S_OK;
}

}
