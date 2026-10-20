
namespace render {

inline void Device12::shade(const float* worldPos, const float* normal, const float* vertexDiffuse,
                            const float* vertexSpecular, float* out) {
    const float* md = rs[kRsDiffuseSource] == 0 ? material[0] : vertexDiffuse;
    const float* ma = rs[kRsAmbientSource] == 0   ? material[1]
                      : rs[kRsAmbientSource] == 1 ? vertexDiffuse
                                                  : vertexSpecular;
    float ambient[3] = {((rs[kRsAmbient] >> 16) & 0xFF) / 255.0f,
                        ((rs[kRsAmbient] >> 8) & 0xFF) / 255.0f,
                        ((rs[kRsAmbient]) & 0xFF) / 255.0f};
    float sum[3] = {material[3][0] + ma[0] * ambient[0], material[3][1] + ma[1] * ambient[1],
                    material[3][2] + ma[2] * ambient[2]};
    for (unsigned i = 0; i < kMaxLights; ++i) {
        if (!lightOn[i]) continue;
        const Light8& l = lights[i];
        float lx = l.direction[0], ly = l.direction[1], lz = l.direction[2];
        float atten = 1.0f;
        if (light_uses_position(l.type)) {
            lx = l.position[0] - worldPos[0];
            ly = l.position[1] - worldPos[1];
            lz = l.position[2] - worldPos[2];
            float dist = lx * lx + ly * ly + lz * lz;
            float d = dist > 0 ? sqrtf(dist) : 0.0f;
            if (d > 0) {
                lx /= d;
                ly /= d;
                lz /= d;
            }
            float denom = l.attenuation0 + l.attenuation1 * d + l.attenuation2 * dist;
            atten = denom > 0 ? 1.0f / denom : 1.0f;
        } else {
            lx = -lx;
            ly = -ly;
            lz = -lz;
            float len = lx * lx + ly * ly + lz * lz;
            len = len > 0 ? sqrtf(len) : 1.0f;
            lx /= len;
            ly /= len;
            lz /= len;
        }
        float ndotl = normal[0] * lx + normal[1] * ly + normal[2] * lz;
        if (ndotl <= 0) continue;
        sum[0] += md[0] * l.diffuse[0] * ndotl * atten;
        sum[1] += md[1] * l.diffuse[1] * ndotl * atten;
        sum[2] += md[2] * l.diffuse[2] * ndotl * atten;
    }
    out[0] = sum[0] < 0 ? 0 : (sum[0] > 1 ? 1 : sum[0]);
    out[1] = sum[1] < 0 ? 0 : (sum[1] > 1 ? 1 : sum[1]);
    out[2] = sum[2] < 0 ? 0 : (sum[2] > 1 ? 1 : sum[2]);
    out[3] = md[3] < 0 ? 0 : (md[3] > 1 ? 1 : md[3]);
}

inline void Device12::stage_texcoord(unsigned stage, const float* uv, unsigned texCount,
                                     const float* camPos, const float* camNormal, float* out) {
    const DWORD tci = tss[stage][11];
    const unsigned set = texcoord_set(tci);
    const unsigned source = texcoord_source(tci);
    float s = 0, t = 0, r = 0;
    if (source == 0) {
        if (uv && set < texCount) {
            s = uv[set * 2];
            t = uv[set * 2 + 1];
        }
    } else if (camPos) {
        if (source == 2) {
            s = camPos[0];
            t = camPos[1];
            r = camPos[2];
        } else if (source == 3 && camNormal) {
            float len = sqrtf(camPos[0] * camPos[0] + camPos[1] * camPos[1] + camPos[2] * camPos[2]);
            if (len > 0) {
                float e[3] = {camPos[0] / len, camPos[1] / len, camPos[2] / len};
                float d = 2.0f * (camNormal[0] * e[0] + camNormal[1] * e[1] + camNormal[2] * e[2]);
                s = d * camNormal[0] - e[0];
                t = d * camNormal[1] - e[1];
                r = d * camNormal[2] - e[2];
            }
        }
    }
    const DWORD flags = tss[stage][24];
    const unsigned count = flags & 7u;
    if (count) {
        const float* m = textureMatrix[stage];
        float x = s * m[0] + t * m[4] + r * m[8] + m[12];
        float y = s * m[1] + t * m[5] + r * m[9] + m[13];
        float z = s * m[2] + t * m[6] + r * m[10] + m[14];
        float w = s * m[3] + t * m[7] + r * m[11] + m[15];
        s = x;
        t = y;
        if (flags & 0x100u) {
            float div = count >= 4 ? w : z;
            if (div != 0) {
                s /= div;
                t /= div;
            }
        }
    }
    out[0] = s;
    out[1] = t;
}

inline void Device12::transform_vertices(const unsigned char* base, const UINT* indexList,
                                         unsigned count, int firstIndex, unsigned char* out) {
    DWORD positionType = fvf_position(fvf);
    bool hasNormal = (fvf & 0x010) != 0;
    bool hasDiffuse = (fvf & 0x040) != 0;
    bool hasSpecular = (fvf & 0x080) != 0;
    unsigned texCount = (fvf >> 8) & 0xF;
    bool lighting = rs[kRsLighting] != 0 && hasNormal;
    float worldView[16];
    mul4(world[0], view, worldView);
    float all[16];
    mul4(worldView, proj, all);
    bool rhw = positionType == 0x004;

    for (unsigned i = 0; i < count; ++i) {
        UINT vertexIndex = indexList ? indexList[i] : (UINT)(firstIndex + (int)i);
        const unsigned char* v = base + (size_t)vertexIndex * streamStride;
        const float* normal = nullptr;
        const float* uv = nullptr;
        float camPos[3] = {0, 0, 0};
        float camNormal[3] = {0, 0, 0};
        float vertexDiffuse[4] = {1, 1, 1, 1};
        float vertexSpecular[4] = {0, 0, 0, 0};

        unsigned specPacked = 0xFFFFFFFFu;
        float clip[4];
        float color[4];
        unsigned offset = 0;
        if (rhw) {
            float x = *(const float*)(v + 0);
            float y = *(const float*)(v + 4);
            float z = *(const float*)(v + 8);
            float rhwValue = *(const float*)(v + 12);
            float w = rhwValue != 0 ? 1.0f / rhwValue : 1.0f;
            clip[0] = (x * 2.0f / (float)width - 1.0f) * w;
            clip[1] = (1.0f - y * 2.0f / (float)height) * w;
            clip[2] = z * w;
            clip[3] = w;
            offset = 16;
        } else {
            float p[4] = {*(const float*)(v + 0), *(const float*)(v + 4), *(const float*)(v + 8),
                          1.0f};
            for (int r = 0; r < 4; ++r) {
                clip[r] = p[0] * all[0 * 4 + r] + p[1] * all[1 * 4 + r] + p[2] * all[2 * 4 + r] +
                          all[3 * 4 + r];
            }

            for (int r = 0; r < 3; ++r) {
                camPos[r] = p[0] * worldView[0 * 4 + r] + p[1] * worldView[1 * 4 + r] +
                            p[2] * worldView[2 * 4 + r] + worldView[3 * 4 + r];
            }
            offset = 12;
        }
        if (hasNormal) {
            normal = (const float*)(v + offset);
            offset += 12;
        }
        if (fvf & 0x020) offset += 4;
        if (hasDiffuse) {
            DWORD d = *(const DWORD*)(v + offset);
            vertexDiffuse[0] = ((d >> 16) & 0xFF) / 255.0f;
            vertexDiffuse[1] = ((d >> 8) & 0xFF) / 255.0f;
            vertexDiffuse[2] = (d & 0xFF) / 255.0f;
            vertexDiffuse[3] = ((d >> 24) & 0xFF) / 255.0f;
            offset += 4;
        }
        if (hasSpecular) {
            DWORD s = *(const DWORD*)(v + offset);
            vertexSpecular[0] = ((s >> 16) & 0xFF) / 255.0f;
            vertexSpecular[1] = ((s >> 8) & 0xFF) / 255.0f;
            vertexSpecular[2] = (s & 0xFF) / 255.0f;
            vertexSpecular[3] = ((s >> 24) & 0xFF) / 255.0f;

            specPacked = ((s >> 16) & 0xFF) | (s & 0x0000FF00u) | ((s & 0xFF) << 16) |
                         (s & 0xFF000000u);
            offset += 4;
        }
        if (texCount) uv = (const float*)(v + offset);
        if (hasNormal) {
            for (int r = 0; r < 3; ++r) {
                camNormal[r] = normal[0] * worldView[0 * 4 + r] + normal[1] * worldView[1 * 4 + r] +
                               normal[2] * worldView[2 * 4 + r];
            }
            float len = sqrtf(camNormal[0] * camNormal[0] + camNormal[1] * camNormal[1] +
                              camNormal[2] * camNormal[2]);
            if (len > 0) {
                camNormal[0] /= len;
                camNormal[1] /= len;
                camNormal[2] /= len;
            }
        }

        if (rhw || !lighting) {

            const bool lit = rs[kRsLighting] != 0;
            if (!rhw && lit && rs[kRsDiffuseSource] == 0) {
                std::memcpy(color, material[0], sizeof color);
            } else {
                std::memcpy(color, vertexDiffuse, sizeof color);
            }
        } else {
            const float* p = (const float*)(v + 0);
            float worldPos[3] = {p[0] * world[0][0] + p[1] * world[0][4] + p[2] * world[0][8] +
                                     world[0][12],
                                 p[0] * world[0][1] + p[1] * world[0][5] + p[2] * world[0][9] +
                                     world[0][13],
                                 p[0] * world[0][2] + p[1] * world[0][6] + p[2] * world[0][10] +
                                     world[0][14]};
            float n[3] = {normal[0] * world[0][0] + normal[1] * world[0][4] + normal[2] * world[0][8],
                          normal[0] * world[0][1] + normal[1] * world[0][5] + normal[2] * world[0][9],
                          normal[0] * world[0][2] + normal[1] * world[0][6] + normal[2] * world[0][10]};
            float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (len > 0) {
                n[0] /= len;
                n[1] /= len;
                n[2] /= len;
            }
            shade(worldPos, n, vertexDiffuse, vertexSpecular, color);
        }

        float tex0[2] = {0, 0};
        float tex1[2] = {0, 0};
        stage_texcoord(0, uv, texCount, rhw ? nullptr : camPos, hasNormal ? camNormal : nullptr,
                       tex0);
        stage_texcoord(1, uv, texCount, rhw ? nullptr : camPos, hasNormal ? camNormal : nullptr,
                       tex1);

        float* o = (float*)(out + (size_t)i * kVertexStride);
        o[0] = clip[0];
        o[1] = clip[1];
        o[2] = clip[2];
        o[3] = clip[3];
        o[4] = color[0];
        o[5] = color[1];
        o[6] = color[2];
        o[7] = color[3];
        o[8] = tex0[0];
        o[9] = tex0[1];
        o[10] = tex1[0];
        o[11] = tex1[1];
        o[12] = clip[3] != 0 ? 1.0f / clip[3] : 0.0f;
        ((unsigned*)o)[13] = specPacked;
    }
}

inline unsigned Device12::emit(const unsigned char* base, const UINT* indexList, unsigned count,
                               int firstIndex) {
    unsigned bytes = count * kVertexStride;
    bytes = (bytes + 15u) & ~15u;
    if (uploadUsed + bytes > uploadSize) {
        ++skipped;
        return 0xFFFFFFFFu;
    }
    unsigned offset = uploadUsed;
    transform_vertices(base, indexList, count, firstIndex, uploadMap + offset);
    uploadUsed += bytes;
    return offset;
}

inline void Device12::census_combo() {
    DWORD v[16] = {tss[0][1], tss[0][2], tss[0][3], tss[0][4], tss[0][5], tss[0][6],
                   tss[1][1], tss[1][2], tss[1][3], tss[1][4], tss[1][5], tss[1][6],
                   tss[0][11], tss[0][24], tss[1][11], tss[1][24]};
    for (unsigned i = 0; i < comboCount; ++i) {
        if (!std::memcmp(combos[i].v, v, sizeof v)) {
            ++combos[i].draws;
            goto counted;
        }
    }
    if (comboCount < 24) {
        std::memcpy(combos[comboCount].v, v, sizeof v);
        combos[comboCount].draws = 1;
        ++comboCount;
    }
counted:
    if (v[6] != 1 && textures[1] && textures[1]->gpuRes) ++stage1Draws;
    if ((v[13] & 7) || (v[15] & 7)) ++projectedDraws;
    if (rs[kRsFogEnable] && (rs[kRsFogTableMode] || rs[kRsFogVertexMode])) ++fogDraws;
    if ((v[1] & 0xF) == 3 || (v[2] & 0xF) == 3 || (v[7] & 0xF) == 3 || (v[8] & 0xF) == 3)
        ++factorDraws;
    if (v[0] == 5 || v[6] == 5) ++mod2xDraws;
    if (v[0] == 18 || v[6] == 18) ++modAlphaAddDraws;

    if (arg_modified(v[1]) || arg_modified(v[2]) || arg_modified(v[4]) || arg_modified(v[5]) ||
        arg_modified(v[7]) || arg_modified(v[8]) || arg_modified(v[10]) || arg_modified(v[11]))
        ++modArgDraws;
}

inline float tex_alpha(Texture12* t, float u, float v) {
    if (!t || !t->data[0][0] || t->format != 21 || !t->width || !t->height) return 1.0f;
    float fu = u - (float)(int)u;
    float fv = v - (float)(int)v;
    if (fu < 0.0f) fu += 1.0f;
    if (fv < 0.0f) fv += 1.0f;
    unsigned x = (unsigned)(fu * (float)t->width);
    unsigned y = (unsigned)(fv * (float)t->height);
    if (x >= t->width) x = t->width - 1;
    if (y >= t->height) y = t->height - 1;
    return t->data[0][0][(y * t->width + x) * 4 + 3] / 255.0f;
}

inline void raster_ids(Device12& d, const unsigned* index, unsigned count, const float* v) {
    for (unsigned t = 0; t + 2 < count; t += 3) {
        const float* a = v + (size_t)index[t] * 14;
        const float* b = v + (size_t)index[t + 1] * 14;
        const float* c = v + (size_t)index[t + 2] * 14;
        if (a[3] <= 0.0f || b[3] <= 0.0f || c[3] <= 0.0f) continue;
        float ax = (a[0] / a[3] + 1.0f) * 0.5f * (float)d.width;
        float ay = (1.0f - a[1] / a[3]) * 0.5f * (float)d.height;
        float az = a[2] / a[3];
        float bx = (b[0] / b[3] + 1.0f) * 0.5f * (float)d.width;
        float by = (1.0f - b[1] / b[3]) * 0.5f * (float)d.height;
        float bz = b[2] / b[3];
        float cx = (c[0] / c[3] + 1.0f) * 0.5f * (float)d.width;
        float cy = (1.0f - c[1] / c[3]) * 0.5f * (float)d.height;
        float cz = c[2] / c[3];
        float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (area == 0.0f) continue;
        float lx = ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx);
        float hx = ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx);
        float ly = ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy);
        float hy = ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy);
        int x0 = (int)lx < 0 ? 0 : (int)lx;
        int x1 = (int)hx >= (int)d.width ? (int)d.width - 1 : (int)hx;
        int y0 = (int)ly < 0 ? 0 : (int)ly;
        int y1 = (int)hy >= (int)d.height ? (int)d.height - 1 : (int)hy;
        for (int y = y0; y <= y1; ++y) {
            float py = (float)y + 0.5f;
            for (int x = x0; x <= x1; ++x) {
                float px = (float)x + 0.5f;
                float e0 = (cx - bx) * (py - by) - (cy - by) * (px - bx);
                float e1 = (ax - cx) * (py - cy) - (ay - cy) * (px - cx);
                float e2 = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
                bool inside = area > 0.0f ? (e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f)
                                          : (e0 <= 0.0f && e1 <= 0.0f && e2 <= 0.0f);
                if (!inside) continue;
                float z = (e0 * az + e1 * bz + e2 * cz) / area;
                if (z < 0.0f || z > 1.0f) continue;
                if ((d.rs[kRsBlendEnable] & 0xF) && (d.rs[kRsSrcBlend] & 0xF) >= 5) {
                    float w0 = e0 / area, w1 = e1 / area, w2 = e2 / area;
                    float alpha = w0 * a[7] + w1 * b[7] + w2 * c[7];
                    float u = w0 * a[8] + w1 * b[8] + w2 * c[8];
                    float t = w0 * a[9] + w1 * b[9] + w2 * c[9];
                    alpha *= tex_alpha(d.textures[0], u, t);
                    if (alpha <= 0.0f) continue;
                }
                unsigned at = (unsigned)y * d.width + (unsigned)x;
                bool pass = true;
                if (d.rs[kRsZEnable] & 0xF) {
                    float have = d.idDepth[at];
                    switch (d.rs[kRsZFunc] & 0xF) {
                        case 1: pass = false; break;
                        case 2: pass = z < have; break;
                        case 3: pass = z == have; break;
                        case 4: pass = z <= have; break;
                        case 5: pass = z > have; break;
                        case 6: pass = z != have; break;
                        case 7: pass = z >= have; break;
                        default: break;
                    }
                }
                if (!pass) continue;
                d.idMap[at] = d.idDraw;
                if (d.rs[kRsZWrite] & 0xF) d.idDepth[at] = z;
            }
        }
    }
}

inline HRESULT Device12::draw_triangles(unsigned primitive, const unsigned char* base,
                                        const UINT* indexList, unsigned vertices, int firstIndex,
                                        const char* source) {
    (void)source;
    if (!list) return E_FAIL;
    if (frameFailed) {

        ++skipped;
        return E_FAIL;
    }
    unsigned triangles = 0;
    if (primitive == 4) {
        triangles = vertices / 3;
    } else if (primitive == 5 || primitive == 6) {
        triangles = vertices > 2 ? vertices - 2 : 0;
    } else {
        ++skipped;
        if (debug_flag()) {
            log_line("# d3d12 drop src=%s prim=%u verts=%u fvf=%08X points-or-lines\n", source,
                     primitive, vertices, (unsigned)fvf);
        }
        return S_OK;
    }
    if (!triangles) {
        if (debug_flag()) {
            log_line("# d3d12 drop src=%s prim=%u verts=%u fvf=%08X no-triangles\n", source,
                     primitive, vertices, (unsigned)fvf);
        }
        return S_OK;
    }
    if (vertices > (1u << 20)) {
        ++skipped;
        return S_OK;
    }

    if (pairUsed == pairCapacity) flush_draws();
    if (frameFailed) {
        ++skipped;
        return E_FAIL;
    }

    unsigned outCount = primitive == 4 ? vertices : triangles * 3;
    UINT* expanded = new UINT[outCount ? outCount : 1];
    unsigned expandedCount = expand_indices(primitive, indexList, vertices, expanded, outCount);
    if (!expandedCount) {
        delete[] expanded;
        ++skipped;
        if (debug_flag()) {
            log_line("# d3d12 drop src=%s prim=%u verts=%u block=%u expand-refused\n", source,
                     primitive, vertices, outCount);
        }
        return S_OK;
    }
    unsigned offset = emit(base, expanded, expandedCount, firstIndex);
    if (idEnabled && presents == rowPresent && idMap) {
        ++idDraw;
        raster_ids(*this, expanded, expandedCount, (const float*)(uploadMap + offset));
    }
    delete[] expanded;
    if (offset == 0xFFFFFFFFu) {
        if (debug_flag()) {
            log_line("# d3d12 skip emit used=%u size=%u verts=%u\n", uploadUsed, uploadSize,
                     vertices);
        }
        return S_OK;
    }

    ID3D12PipelineState* state = pipeline();
    if (!state) {
        ++skipped;
        if (debug_flag()) {
            log_line("# d3d12 skip pso verts=%u fvf=%08X target=%u ztest=%u\n", vertices,
                     (unsigned)fvf, targetTexture ? 1u : 0u, (unsigned)rs[kRsZEnable]);
        }
        return S_OK;
    }
    list->SetPipelineState(state);

    render::Params params = {};
    for (unsigned stage = 0; stage < 2; ++stage) {
        unsigned* p = stage == 0 ? &params.op0 : &params.op1;
        bool bound = textures[stage] && textures[stage]->gpuRes;
        *p = bound ? tss[stage][1] : 1;
        params.flags |= bound ? (stage == 0 ? 2u : 4u) : 0u;
    }
    params.arg00 = tss[0][2];
    params.arg01 = tss[0][3];
    params.aop0 = tss[0][4];
    params.aarg00 = tss[0][5];
    params.aarg01 = tss[0][6];
    params.arg10 = tss[1][2];
    params.arg11 = tss[1][3];
    params.aop1 = tss[1][4];
    params.aarg10 = tss[1][5];
    params.aarg11 = tss[1][6];
    DWORD textureFactor = rs[kRsTextureFactor];
    params.factor[0] = ((textureFactor >> 16) & 0xFF) / 255.0f;
    params.factor[1] = ((textureFactor >> 8) & 0xFF) / 255.0f;
    params.factor[2] = (textureFactor & 0xFF) / 255.0f;
    params.factor[3] = ((textureFactor >> 24) & 0xFF) / 255.0f;
    DWORD fogColor = rs[kRsFogColor];
    params.fogColor[0] = ((fogColor >> 16) & 0xFF) / 255.0f;
    params.fogColor[1] = ((fogColor >> 8) & 0xFF) / 255.0f;
    params.fogColor[2] = (fogColor & 0xFF) / 255.0f;
    params.fogColor[3] = 1.0f;
    std::memcpy(&params.fogStart, &rs[kRsFogStart], 4);
    std::memcpy(&params.fogEnd, &rs[kRsFogEnd], 4);
    const unsigned fogTable = rs[kRsFogTableMode], fogVertex = rs[kRsFogVertexMode];
    if (fog_applies(rs[kRsFogEnable] != 0, fogTable, fogVertex)) {
        params.flags |= 8;
    } else if (rs[kRsFogEnable] && (fogTable || fogVertex)) {

        if (fogUnsupported++ == 0)
            log_line("# d3d12 fog mode unsupported table=%u vertex=%u linear_only\n", fogTable,
                     fogVertex);
    }
    if (forceAlpha) params.flags |= 16;
    if (forceTexture) params.flags |= 64;
    params.alphaRef = (rs[kRsAlphaRef] & 0xFF) / 255.0f;

    params.alphaFunc = rs[kRsAlphaFunc] & 15;
    if (rs[kRsAlphaTest]) params.flags |= 1;

    params.stageCount = effective_stages(params.op0, params.op1);
    params.flags |= mip_filter_flag(0, tss[0][18], textures[0] && textures[0]->gpuRes);
    params.flags |= mip_filter_flag(1, tss[1][18], textures[1] && textures[1]->gpuRes);
    ++stageOps;
    census_combo();
    unsigned cbOffset = (uploadUsed + 255u) & ~255u;
    if (cbOffset + 256 > uploadSize) {
        ++skipped;
        return S_OK;
    }
    std::memcpy(uploadMap + cbOffset, &params, sizeof params);
    uploadUsed = cbOffset + 256;
    list->SetGraphicsRootConstantBufferView(1, upload->GetGPUVirtualAddress() + cbOffset);

    unsigned pair = pair_base(pairUsed++);
    list->SetGraphicsRootDescriptorTable(0, srv_gpu(pair));
    bind_texture(textures[0], 0, pair);
    if (frameFailed) {

        ++skipped;
        return E_FAIL;
    }
    bind_texture(textures[1], 1, pair);
    if (frameFailed) {
        ++skipped;
        return E_FAIL;
    }

    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = upload->GetGPUVirtualAddress() + offset;
    vbv.SizeInBytes = outCount * kVertexStride;
    vbv.StrideInBytes = kVertexStride;
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 1, &vbv);
    list->DrawInstanced(expandedCount, 1, 0, 0);
    ++draws;
    if (debug_flag() && (debug_draws() || presents == rowPresent)) {

        const float* v = (const float*)(uploadMap + offset);
        const float* raw = (const float*)(base);
        unsigned step = streamStride / 4;

        int bx0 = 1 << 30, by0 = 1 << 30, bx1 = -(1 << 30), by1 = -(1 << 30);
        float alpha = 0.0f;
        float zmin = 1.0f, zmax = 0.0f;
        for (unsigned k = 0; k < outCount; ++k) {
            const float* p = v + k * 14;
            float w = p[3] != 0 ? p[3] : 1.0f;
            int x = (int)((p[0] / w + 1.0f) * 0.5f * (float)width);
            int y = (int)((1.0f - p[1] / w) * 0.5f * (float)height);
            if (x < bx0) bx0 = x;
            if (x > bx1) bx1 = x;
            if (y < by0) by0 = y;
            if (y > by1) by1 = y;
            float z = p[2] / w;
            if (z < zmin) zmin = z;
            if (z > zmax) zmax = z;
            alpha += p[7];
        }
        alpha = outCount ? alpha / (float)outCount : 0.0f;
        log_line("# d3d12 draw src=%s prim=%u verts=%u fvf=%08X tex=%u tex1=%u target=%u "
                 "stride=%u op=%u op1=%u aop=%u/%u/%u atest=%u/%u/%u tci1=%08X tff1=%08X "
                 "blend=%u/%u/%u zen=%u zw=%u zf=%u "
                 "texid=%08X texdim=%ux%u texfmt=%u tex1fmt=%u fog=%u mipnone=%u light=%u "
                 "tff0=%X tci0=%08X vp=%f,%f,%fx%f "
                 "z=%f..%f bbox=%d,%d,%d,%d "
                 "a=%f raw0=%f,%f,%f,%f raw1=%f,%f,%f,%f "
                 "clip0=%f,%f,%f,%f clip1=%f,%f,%f,%f clip2=%f,%f,%f,%f col0=%f,%f,%f,%f "
                 "uv0=%f,%f uv1=%f,%f\n",
                 source, primitive, vertices, (unsigned)fvf, textures[0] ? 1u : 0u,
                 textures[1] ? 1u : 0u, targetTexture ? 1u : 0u, (unsigned)streamStride, params.op0,
                 params.op1, (unsigned)tss[0][4], (unsigned)tss[0][5], (unsigned)tss[0][6],
                 (unsigned)rs[kRsAlphaTest], (unsigned)rs[kRsAlphaFunc],
                 (unsigned)rs[kRsAlphaRef], (unsigned)tss[1][11], (unsigned)tss[1][24],
                 (unsigned)rs[kRsBlendEnable],
                 (unsigned)rs[kRsSrcBlend], (unsigned)rs[kRsDestBlend], (unsigned)rs[kRsZEnable],
                 (unsigned)rs[kRsZWrite], (unsigned)rs[kRsZFunc],
                 textures[0] ? (unsigned)(uintptr_t)textures[0] : 0u,
                 textures[0] ? textures[0]->width : 0u, textures[0] ? textures[0]->height : 0u,
                 textures[0] ? textures[0]->format : 0u,
                 textures[1] ? textures[1]->format : 0u, (params.flags >> 3) & 1u,
                 (params.flags >> 7) & 3u, (unsigned)rs[kRsLighting],
                 (unsigned)tss[0][24], (unsigned)tss[0][11], viewport[0], viewport[1],
                 viewport[2], viewport[3], zmin, zmax, bx0, by0, bx1, by1,
                 alpha,
                 raw[0], raw[1], raw[2], raw[3], raw[step], raw[step + 1], raw[step + 2],
                 raw[step + 3], v[0], v[1], v[2], v[3], v[14], v[15], v[16], v[17], v[28], v[29],
                 v[30], v[31], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11]);
        if (xfDirty) {
            xfDirty = false;
            const float* m[3] = {view, proj, world[0]};
            static const char* const kNames[3] = {"view", "proj", "world0"};
            for (int k = 0; k < 3; ++k) {
                log_line("# d3d12 xf %s %f,%f,%f,%f %f,%f,%f,%f %f,%f,%f,%f %f,%f,%f,%f\n",
                         kNames[k], m[k][0], m[k][1], m[k][2], m[k][3], m[k][4], m[k][5], m[k][6],
                         m[k][7], m[k][8], m[k][9], m[k][10], m[k][11], m[k][12], m[k][13],
                         m[k][14], m[k][15]);
            }
        }
    }
    return S_OK;
}

inline HRESULT Device12::DrawPrimitive(unsigned primitive, UINT start, UINT count) {
    if (render::skip_draw(70)) return S_OK;
    ++callsPrimitive;
    if (!stream) {
        ++drops;
        return S_OK;
    }
    return draw_triangles(primitive, stream->data, nullptr, primitive_vertices(primitive, count),
                          (int)start, "primitive");
}

inline HRESULT Device12::DrawIndexedPrimitive(unsigned primitive, UINT minIndex, UINT numVertices,
                                              UINT startIndex, UINT count) {
    if (render::skip_draw(71)) return S_OK;
    (void)minIndex;
    (void)numVertices;
    ++callsIndexed;
    if (!stream || !indices) {
        ++drops;
        return S_OK;
    }
    unsigned vertices = primitive_vertices(primitive, count);
    if (!vertices) return S_OK;
    IndexTrace& trace = index_trace();
    trace.list = indices->data;
    trace.primitive = primitive;
    trace.vertices = vertices;
    trace.startIndex = startIndex;
    trace.count = count;
    trace.length = indices->length;
    trace.format = indices->format;
    if (!trace.reported && index_overruns(startIndex, vertices, indices->length, indices->format)) {
        trace.reported = 1;
        log_line("# d3d12 index guard start=%u verts=%u count=%u length=%u format=%u prim=%u\n",
                 startIndex, vertices, count, indices->length, indices->format, primitive);
    }
    vertices = clamp_index_count(startIndex, vertices, indices->length, indices->format);
    if (!vertices) {
        ++drops;
        return S_OK;
    }
    const UINT* list32 = (const UINT*)indices->data;
    if (indices->format == 102) {
        return draw_triangles(primitive, stream->data, list32 + startIndex, vertices, 0,
                              "indexed32");
    }
    UINT* list16 = new UINT[vertices];
    const unsigned short* src = (const unsigned short*)indices->data + startIndex;
    for (unsigned i = 0; i < vertices; ++i) list16[i] = (UINT)src[i] + baseVertex;
    HRESULT hr = draw_triangles(primitive, stream->data, list16, vertices, 0, "indexed");
    delete[] list16;
    return hr;
}

inline HRESULT Device12::DrawPrimitiveUP(unsigned primitive, UINT count, const void* data,
                                         UINT stride) {
    if (render::skip_draw(72)) return S_OK;
    ++callsUp;
    if (!data) {
        ++drops;
        return S_OK;
    }
    unsigned saved = streamStride;
    streamStride = stride;
    HRESULT hr = draw_triangles(primitive, (const unsigned char*)data, nullptr,
                                primitive_vertices(primitive, count), 0, "up");
    streamStride = saved;
    return hr;
}

inline Device* create_d3d12(HWND window, unsigned width, unsigned height, DWORD format,
                            DWORD behavior, const unsigned* capsDwords) {
    Device12* device = new Device12;
    if (!device->init(window, width, height, format, behavior, capsDwords)) {
        device->shutdown();
        delete device;
        return nullptr;
    }
    backend_name_slot() = "d3d12";
    set_device(device);
    return device;
}

}
