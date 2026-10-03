#pragma once

static HINSTANCE hOriginalBinkW32 = NULL;
static FARPROC hFuncEntry[72] = { 0 };

// 注意：下方每个 stub 中的 `jmp hFuncEntry[i * 4]` 是正确的写法——
// MSVC 内联汇编把 `hFuncEntry[n]` 的 n 当作字节偏移（不按元素类型缩放），
// i * 4 字节偏移恰好等于 FARPROC 数组的第 i 项。
// 请勿改成 hFuncEntry[i]，那会错误地跳到第 i/4 项。

static void LoadBinkw32FuncEntry()
{
	hOriginalBinkW32 = LoadLibrary(L"binkw32_o.dll");
	if (!hOriginalBinkW32)
	{
		return;
	}
	hFuncEntry[0] = GetProcAddress(hOriginalBinkW32, "_BinkBufferBlit@12");
	hFuncEntry[1] = GetProcAddress(hOriginalBinkW32, "_BinkBufferCheckWinPos@12");
	hFuncEntry[2] = GetProcAddress(hOriginalBinkW32, "_BinkBufferClear@8");
	hFuncEntry[3] = GetProcAddress(hOriginalBinkW32, "_BinkBufferClose@4");
	hFuncEntry[4] = GetProcAddress(hOriginalBinkW32, "_BinkBufferGetDescription@4");
	hFuncEntry[5] = GetProcAddress(hOriginalBinkW32, "_BinkBufferGetError@0");
	hFuncEntry[6] = GetProcAddress(hOriginalBinkW32, "_BinkBufferLock@4");
	hFuncEntry[7] = GetProcAddress(hOriginalBinkW32, "_BinkBufferOpen@16");
	hFuncEntry[8] = GetProcAddress(hOriginalBinkW32, "_BinkBufferSetDirectDraw@8");
	hFuncEntry[9] = GetProcAddress(hOriginalBinkW32, "_BinkBufferSetHWND@8");
	hFuncEntry[10] = GetProcAddress(hOriginalBinkW32, "_BinkBufferSetOffset@12");
	hFuncEntry[11] = GetProcAddress(hOriginalBinkW32, "_BinkBufferSetResolution@12");
	hFuncEntry[12] = GetProcAddress(hOriginalBinkW32, "_BinkBufferSetScale@12");
	hFuncEntry[13] = GetProcAddress(hOriginalBinkW32, "_BinkBufferUnlock@4");
	hFuncEntry[14] = GetProcAddress(hOriginalBinkW32, "_BinkCheckCursor@20");
	hFuncEntry[15] = GetProcAddress(hOriginalBinkW32, "_BinkClose@4");
	hFuncEntry[16] = GetProcAddress(hOriginalBinkW32, "_BinkCloseTrack@4");
	hFuncEntry[17] = GetProcAddress(hOriginalBinkW32, "_BinkControlBackgroundIO@8");
	hFuncEntry[18] = GetProcAddress(hOriginalBinkW32, "_BinkControlPlatformFeatures@8");
	hFuncEntry[19] = GetProcAddress(hOriginalBinkW32, "_BinkCopyToBuffer@28");
	hFuncEntry[20] = GetProcAddress(hOriginalBinkW32, "_BinkCopyToBufferRect@44");
	hFuncEntry[21] = GetProcAddress(hOriginalBinkW32, "_BinkDDSurfaceType@4");
	hFuncEntry[22] = GetProcAddress(hOriginalBinkW32, "_BinkDX8SurfaceType@4");
	hFuncEntry[23] = GetProcAddress(hOriginalBinkW32, "_BinkDX9SurfaceType@4");
	hFuncEntry[24] = GetProcAddress(hOriginalBinkW32, "_BinkDoFrame@4");
	hFuncEntry[25] = GetProcAddress(hOriginalBinkW32, "_BinkDoFrameAsync@12");
	hFuncEntry[26] = GetProcAddress(hOriginalBinkW32, "_BinkDoFrameAsyncWait@8");
	hFuncEntry[27] = GetProcAddress(hOriginalBinkW32, "_BinkDoFramePlane@8");
	hFuncEntry[28] = GetProcAddress(hOriginalBinkW32, "_BinkGetError@0");
	hFuncEntry[29] = GetProcAddress(hOriginalBinkW32, "_BinkGetFrameBuffersInfo@8");
	hFuncEntry[30] = GetProcAddress(hOriginalBinkW32, "_BinkGetKeyFrame@12");
	hFuncEntry[31] = GetProcAddress(hOriginalBinkW32, "_BinkGetPalette@4");
	hFuncEntry[32] = GetProcAddress(hOriginalBinkW32, "_BinkGetRealtime@12");
	hFuncEntry[33] = GetProcAddress(hOriginalBinkW32, "_BinkGetRects@8");
	hFuncEntry[34] = GetProcAddress(hOriginalBinkW32, "_BinkGetSummary@8");
	hFuncEntry[35] = GetProcAddress(hOriginalBinkW32, "_BinkGetTrackData@8");
	hFuncEntry[36] = GetProcAddress(hOriginalBinkW32, "_BinkGetTrackID@8");
	hFuncEntry[37] = GetProcAddress(hOriginalBinkW32, "_BinkGetTrackMaxSize@8");
	hFuncEntry[38] = GetProcAddress(hOriginalBinkW32, "_BinkGetTrackType@8");
	hFuncEntry[39] = GetProcAddress(hOriginalBinkW32, "_BinkGoto@12");
	hFuncEntry[40] = GetProcAddress(hOriginalBinkW32, "_BinkIsSoftwareCursor@8");
	hFuncEntry[41] = GetProcAddress(hOriginalBinkW32, "_BinkLogoAddress@0");
	hFuncEntry[42] = GetProcAddress(hOriginalBinkW32, "_BinkNextFrame@4");
	hFuncEntry[43] = GetProcAddress(hOriginalBinkW32, "_BinkOpen@8");
	hFuncEntry[44] = GetProcAddress(hOriginalBinkW32, "_BinkOpenDirectSound@4");
	hFuncEntry[45] = GetProcAddress(hOriginalBinkW32, "_BinkOpenMiles@4");
	hFuncEntry[46] = GetProcAddress(hOriginalBinkW32, "_BinkOpenTrack@8");
	hFuncEntry[47] = GetProcAddress(hOriginalBinkW32, "_BinkOpenWaveOut@4");
	hFuncEntry[48] = GetProcAddress(hOriginalBinkW32, "_BinkPause@8");
	hFuncEntry[49] = GetProcAddress(hOriginalBinkW32, "_BinkRegisterFrameBuffers@8");
	hFuncEntry[50] = GetProcAddress(hOriginalBinkW32, "_BinkRequestStopAsyncThread@4");
	hFuncEntry[51] = GetProcAddress(hOriginalBinkW32, "_BinkRestoreCursor@4");
	hFuncEntry[52] = GetProcAddress(hOriginalBinkW32, "_BinkService@4");
	hFuncEntry[53] = GetProcAddress(hOriginalBinkW32, "_BinkSetError@4");
	hFuncEntry[54] = GetProcAddress(hOriginalBinkW32, "_BinkSetFrameRate@8");
	hFuncEntry[55] = GetProcAddress(hOriginalBinkW32, "_BinkSetIO@4");
	hFuncEntry[56] = GetProcAddress(hOriginalBinkW32, "_BinkSetIOSize@4");
	hFuncEntry[57] = GetProcAddress(hOriginalBinkW32, "_BinkSetMemory@8");
	hFuncEntry[58] = GetProcAddress(hOriginalBinkW32, "_BinkSetMixBinVolumes@20");
	hFuncEntry[59] = GetProcAddress(hOriginalBinkW32, "_BinkSetMixBins@16");
	hFuncEntry[60] = GetProcAddress(hOriginalBinkW32, "_BinkSetPan@12");
	hFuncEntry[61] = GetProcAddress(hOriginalBinkW32, "_BinkSetSimulate@4");
	hFuncEntry[62] = GetProcAddress(hOriginalBinkW32, "_BinkSetSoundOnOff@8");
	hFuncEntry[63] = GetProcAddress(hOriginalBinkW32, "_BinkSetSoundSystem@8");
	hFuncEntry[64] = GetProcAddress(hOriginalBinkW32, "_BinkSetSoundTrack@8");
	hFuncEntry[65] = GetProcAddress(hOriginalBinkW32, "_BinkSetVideoOnOff@8");
	hFuncEntry[66] = GetProcAddress(hOriginalBinkW32, "_BinkSetVolume@12");
	hFuncEntry[67] = GetProcAddress(hOriginalBinkW32, "_BinkShouldSkip@4");
	hFuncEntry[68] = GetProcAddress(hOriginalBinkW32, "_BinkStartAsyncThread@8");
	hFuncEntry[69] = GetProcAddress(hOriginalBinkW32, "_BinkWait@4");
	hFuncEntry[70] = GetProcAddress(hOriginalBinkW32, "_BinkWaitStopAsyncThread@4");
	hFuncEntry[71] = GetProcAddress(hOriginalBinkW32, "_RADTimerRead@0");
}

// _BinkBufferBlit@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferBlit(int a1, int a2, int a3)
{
	__asm
	{
		jmp hFuncEntry[0 * 4];
	}
}

// _BinkBufferCheckWinPos@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferCheckWinPos(int a1, int a2, int a3)
{
	__asm
	{
		jmp hFuncEntry[1 * 4];
	}
}

// _BinkBufferClear@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferClear(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[2 * 4];
	}
}

// _BinkBufferClose@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferClose(int a1)
{
	__asm
	{
		jmp hFuncEntry[3 * 4];
	}
}

// _BinkBufferGetDescription@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferGetDescription(int a1)
{
	__asm
	{
		jmp hFuncEntry[4 * 4];
	}
}

// _BinkBufferGetError@0
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferGetError()
{
	__asm
	{
		jmp hFuncEntry[5 * 4];
	}
}

// _BinkBufferLock@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferLock(int a1)
{
	__asm
	{
		jmp hFuncEntry[6 * 4];
	}
}

// _BinkBufferOpen@16
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferOpen(int a1, int a2, int a3, int a4)
{
	__asm
	{
		jmp hFuncEntry[7 * 4];
	}
}

// _BinkBufferSetDirectDraw@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferSetDirectDraw(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[8 * 4];
	}
}

// _BinkBufferSetHWND@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferSetHWND(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[9 * 4];
	}
}

// _BinkBufferSetOffset@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferSetOffset(int a1, int a2, int a3)
{
	__asm
	{
		jmp hFuncEntry[10 * 4];
	}
}

// _BinkBufferSetResolution@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferSetResolution(int a1, int a2, int a3)
{
	__asm
	{
		jmp hFuncEntry[11 * 4];
	}
}

// _BinkBufferSetScale@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferSetScale(int a1, int a2, int a3)
{
	__asm
	{
		jmp hFuncEntry[12 * 4];
	}
}

// _BinkBufferUnlock@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferUnlock(int a1)
{
	__asm
	{
		jmp hFuncEntry[13 * 4];
	}
}

// _BinkCheckCursor@20
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkCheckCursor(int a1, int a2, int a3, int a4, int a5)
{
	__asm
	{
		jmp hFuncEntry[14 * 4];
	}
}

// _BinkClose@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkClose(int a1)
{
	__asm
	{
		jmp hFuncEntry[15 * 4];
	}
}

// _BinkCloseTrack@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkCloseTrack(int a1)
{
	__asm
	{
		jmp hFuncEntry[16 * 4];
	}
}

// _BinkControlBackgroundIO@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkControlBackgroundIO(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[17 * 4];
	}
}

// _BinkControlPlatformFeatures@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkControlPlatformFeatures(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[18 * 4];
	}
}

// _BinkCopyToBuffer@28
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkCopyToBuffer(int a1, int a2, int a3, int a4, int a5, int a6, int a7)
{
	__asm
	{
		jmp hFuncEntry[19 * 4];
	}
}

// _BinkCopyToBufferRect@44
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkCopyToBufferRect(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11)
{
	__asm
	{
		jmp hFuncEntry[20 * 4];
	}
}

// _BinkDDSurfaceType@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkDDSurfaceType(int a1)
{
	__asm
	{
		jmp hFuncEntry[21 * 4];
	}
}

// _BinkDX8SurfaceType@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkDX8SurfaceType(int a1)
{
	__asm
	{
		jmp hFuncEntry[22 * 4];
	}
}

// _BinkDX9SurfaceType@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkDX9SurfaceType(int a1)
{
	__asm
	{
		jmp hFuncEntry[23 * 4];
	}
}

// _BinkDoFrame@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkDoFrame(int a1)
{
	__asm
	{
		jmp hFuncEntry[24 * 4];
	}
}

// _BinkDoFrameAsync@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkDoFrameAsync(int a1, int a2, int a3)
{
	__asm
	{
		jmp hFuncEntry[25 * 4];
	}
}

// _BinkDoFrameAsyncWait@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkDoFrameAsyncWait(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[26 * 4];
	}
}

// _BinkDoFramePlane@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkDoFramePlane(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[27 * 4];
	}
}

// _BinkGetError@0
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetError()
{
	__asm
	{
		jmp hFuncEntry[28 * 4];
	}
}

// _BinkGetFrameBuffersInfo@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetFrameBuffersInfo(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[29 * 4];
	}
}

// _BinkGetKeyFrame@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetKeyFrame(int a1, int a2, int a3)
{
	__asm
	{
		jmp hFuncEntry[30 * 4];
	}
}

// _BinkGetPalette@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetPalette(int a1)
{
	__asm
	{
		jmp hFuncEntry[31 * 4];
	}
}

// _BinkGetRealtime@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetRealtime(int a1, int a2, int a3)
{
	__asm
	{
		jmp hFuncEntry[32 * 4];
	}
}

// _BinkGetRects@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetRects(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[33 * 4];
	}
}

// _BinkGetSummary@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetSummary(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[34 * 4];
	}
}

// _BinkGetTrackData@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetTrackData(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[35 * 4];
	}
}

// _BinkGetTrackID@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetTrackID(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[36 * 4];
	}
}

// _BinkGetTrackMaxSize@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetTrackMaxSize(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[37 * 4];
	}
}

// _BinkGetTrackType@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetTrackType(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[38 * 4];
	}
}

// _BinkGoto@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGoto(int a1, int a2, int a3)
{
	__asm
	{
		jmp hFuncEntry[39 * 4];
	}
}

// _BinkIsSoftwareCursor@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkIsSoftwareCursor(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[40 * 4];
	}
}

// _BinkLogoAddress@0
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkLogoAddress()
{
	__asm
	{
		jmp hFuncEntry[41 * 4];
	}
}

// _BinkNextFrame@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkNextFrame(int a1)
{
	__asm
	{
		jmp hFuncEntry[42 * 4];
	}
}

// _BinkOpen@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkOpen(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[43 * 4];
	}
}

// _BinkOpenDirectSound@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkOpenDirectSound(int a1)
{
	__asm
	{
		jmp hFuncEntry[44 * 4];
	}
}

// _BinkOpenMiles@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkOpenMiles(int a1)
{
	__asm
	{
		jmp hFuncEntry[45 * 4];
	}
}

// _BinkOpenTrack@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkOpenTrack(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[46 * 4];
	}
}

// _BinkOpenWaveOut@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkOpenWaveOut(int a1)
{
	__asm
	{
		jmp hFuncEntry[47 * 4];
	}
}

// _BinkPause@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkPause(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[48 * 4];
	}
}

// _BinkRegisterFrameBuffers@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkRegisterFrameBuffers(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[49 * 4];
	}
}

// _BinkRequestStopAsyncThread@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkRequestStopAsyncThread(int a1)
{
	__asm
	{
		jmp hFuncEntry[50 * 4];
	}
}

// _BinkRestoreCursor@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkRestoreCursor(int a1)
{
	__asm
	{
		jmp hFuncEntry[51 * 4];
	}
}

// _BinkService@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkService(int a1)
{
	__asm
	{
		jmp hFuncEntry[52 * 4];
	}
}

// _BinkSetError@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetError(int a1)
{
	__asm
	{
		jmp hFuncEntry[53 * 4];
	}
}

// _BinkSetFrameRate@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetFrameRate(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[54 * 4];
	}
}

// _BinkSetIO@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetIO(int a1)
{
	__asm
	{
		jmp hFuncEntry[55 * 4];
	}
}

// _BinkSetIOSize@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetIOSize(int a1)
{
	__asm
	{
		jmp hFuncEntry[56 * 4];
	}
}

// _BinkSetMemory@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetMemory(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[57 * 4];
	}
}

// _BinkSetMixBinVolumes@20
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetMixBinVolumes(int a1, int a2, int a3, int a4, int a5)
{
	__asm
	{
		jmp hFuncEntry[58 * 4];
	}
}

// _BinkSetMixBins@16
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetMixBins(int a1, int a2, int a3, int a4)
{
	__asm
	{
		jmp hFuncEntry[59 * 4];
	}
}

// _BinkSetPan@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetPan(int a1, int a2, int a3)
{
	__asm
	{
		jmp hFuncEntry[60 * 4];
	}
}

// _BinkSetSimulate@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetSimulate(int a1)
{
	__asm
	{
		jmp hFuncEntry[61 * 4];
	}
}

// _BinkSetSoundOnOff@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetSoundOnOff(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[62 * 4];
	}
}

// _BinkSetSoundSystem@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetSoundSystem(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[63 * 4];
	}
}

// _BinkSetSoundTrack@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetSoundTrack(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[64 * 4];
	}
}

// _BinkSetVideoOnOff@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetVideoOnOff(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[65 * 4];
	}
}

// _BinkSetVolume@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetVolume(int a1, int a2, int a3)
{
	__asm
	{
		jmp hFuncEntry[66 * 4];
	}
}

// _BinkShouldSkip@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkShouldSkip(int a1)
{
	__asm
	{
		jmp hFuncEntry[67 * 4];
	}
}

// _BinkStartAsyncThread@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkStartAsyncThread(int a1, int a2)
{
	__asm
	{
		jmp hFuncEntry[68 * 4];
	}
}

// _BinkWait@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkWait(int a1)
{
	__asm
	{
		jmp hFuncEntry[69 * 4];
	}
}

// _BinkWaitStopAsyncThread@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkWaitStopAsyncThread(int a1)
{
	__asm
	{
		jmp hFuncEntry[70 * 4];
	}
}

// _RADTimerRead@0
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall RADTimerRead()
{
	__asm
	{
		jmp hFuncEntry[71 * 4];
	}
}
