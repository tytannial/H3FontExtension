#pragma once

// naked stub 的参数仅用于 stdcall 名称修饰（_Name@N 的字节数），函数体不引用，
// 在此头文件内屏蔽未引用参数警告
#pragma warning(push)
#pragma warning(disable: 4100)

static HINSTANCE gOriginalBinkW32 = NULL;
static FARPROC gFuncEntry[72] = { 0 };

// 注意：下方每个 stub 中的 `jmp gFuncEntry[i * 4]` 是正确的写法——
// MSVC 内联汇编把 `gFuncEntry[n]` 的 n 当作字节偏移（不按元素类型缩放），
// i * 4 字节偏移恰好等于 FARPROC 数组的第 i 项。
// 请勿改成 gFuncEntry[i]，那会错误地跳到第 i/4 项。

static void loadBinkw32FuncEntry()
{
	gOriginalBinkW32 = LoadLibrary(L"binkw32_o.dll");
	if (!gOriginalBinkW32)
	{
		return;
	}
	gFuncEntry[0] = GetProcAddress(gOriginalBinkW32, "_BinkBufferBlit@12");
	gFuncEntry[1] = GetProcAddress(gOriginalBinkW32, "_BinkBufferCheckWinPos@12");
	gFuncEntry[2] = GetProcAddress(gOriginalBinkW32, "_BinkBufferClear@8");
	gFuncEntry[3] = GetProcAddress(gOriginalBinkW32, "_BinkBufferClose@4");
	gFuncEntry[4] = GetProcAddress(gOriginalBinkW32, "_BinkBufferGetDescription@4");
	gFuncEntry[5] = GetProcAddress(gOriginalBinkW32, "_BinkBufferGetError@0");
	gFuncEntry[6] = GetProcAddress(gOriginalBinkW32, "_BinkBufferLock@4");
	gFuncEntry[7] = GetProcAddress(gOriginalBinkW32, "_BinkBufferOpen@16");
	gFuncEntry[8] = GetProcAddress(gOriginalBinkW32, "_BinkBufferSetDirectDraw@8");
	gFuncEntry[9] = GetProcAddress(gOriginalBinkW32, "_BinkBufferSetHWND@8");
	gFuncEntry[10] = GetProcAddress(gOriginalBinkW32, "_BinkBufferSetOffset@12");
	gFuncEntry[11] = GetProcAddress(gOriginalBinkW32, "_BinkBufferSetResolution@12");
	gFuncEntry[12] = GetProcAddress(gOriginalBinkW32, "_BinkBufferSetScale@12");
	gFuncEntry[13] = GetProcAddress(gOriginalBinkW32, "_BinkBufferUnlock@4");
	gFuncEntry[14] = GetProcAddress(gOriginalBinkW32, "_BinkCheckCursor@20");
	gFuncEntry[15] = GetProcAddress(gOriginalBinkW32, "_BinkClose@4");
	gFuncEntry[16] = GetProcAddress(gOriginalBinkW32, "_BinkCloseTrack@4");
	gFuncEntry[17] = GetProcAddress(gOriginalBinkW32, "_BinkControlBackgroundIO@8");
	gFuncEntry[18] = GetProcAddress(gOriginalBinkW32, "_BinkControlPlatformFeatures@8");
	gFuncEntry[19] = GetProcAddress(gOriginalBinkW32, "_BinkCopyToBuffer@28");
	gFuncEntry[20] = GetProcAddress(gOriginalBinkW32, "_BinkCopyToBufferRect@44");
	gFuncEntry[21] = GetProcAddress(gOriginalBinkW32, "_BinkDDSurfaceType@4");
	gFuncEntry[22] = GetProcAddress(gOriginalBinkW32, "_BinkDX8SurfaceType@4");
	gFuncEntry[23] = GetProcAddress(gOriginalBinkW32, "_BinkDX9SurfaceType@4");
	gFuncEntry[24] = GetProcAddress(gOriginalBinkW32, "_BinkDoFrame@4");
	gFuncEntry[25] = GetProcAddress(gOriginalBinkW32, "_BinkDoFrameAsync@12");
	gFuncEntry[26] = GetProcAddress(gOriginalBinkW32, "_BinkDoFrameAsyncWait@8");
	gFuncEntry[27] = GetProcAddress(gOriginalBinkW32, "_BinkDoFramePlane@8");
	gFuncEntry[28] = GetProcAddress(gOriginalBinkW32, "_BinkGetError@0");
	gFuncEntry[29] = GetProcAddress(gOriginalBinkW32, "_BinkGetFrameBuffersInfo@8");
	gFuncEntry[30] = GetProcAddress(gOriginalBinkW32, "_BinkGetKeyFrame@12");
	gFuncEntry[31] = GetProcAddress(gOriginalBinkW32, "_BinkGetPalette@4");
	gFuncEntry[32] = GetProcAddress(gOriginalBinkW32, "_BinkGetRealtime@12");
	gFuncEntry[33] = GetProcAddress(gOriginalBinkW32, "_BinkGetRects@8");
	gFuncEntry[34] = GetProcAddress(gOriginalBinkW32, "_BinkGetSummary@8");
	gFuncEntry[35] = GetProcAddress(gOriginalBinkW32, "_BinkGetTrackData@8");
	gFuncEntry[36] = GetProcAddress(gOriginalBinkW32, "_BinkGetTrackID@8");
	gFuncEntry[37] = GetProcAddress(gOriginalBinkW32, "_BinkGetTrackMaxSize@8");
	gFuncEntry[38] = GetProcAddress(gOriginalBinkW32, "_BinkGetTrackType@8");
	gFuncEntry[39] = GetProcAddress(gOriginalBinkW32, "_BinkGoto@12");
	gFuncEntry[40] = GetProcAddress(gOriginalBinkW32, "_BinkIsSoftwareCursor@8");
	gFuncEntry[41] = GetProcAddress(gOriginalBinkW32, "_BinkLogoAddress@0");
	gFuncEntry[42] = GetProcAddress(gOriginalBinkW32, "_BinkNextFrame@4");
	gFuncEntry[43] = GetProcAddress(gOriginalBinkW32, "_BinkOpen@8");
	gFuncEntry[44] = GetProcAddress(gOriginalBinkW32, "_BinkOpenDirectSound@4");
	gFuncEntry[45] = GetProcAddress(gOriginalBinkW32, "_BinkOpenMiles@4");
	gFuncEntry[46] = GetProcAddress(gOriginalBinkW32, "_BinkOpenTrack@8");
	gFuncEntry[47] = GetProcAddress(gOriginalBinkW32, "_BinkOpenWaveOut@4");
	gFuncEntry[48] = GetProcAddress(gOriginalBinkW32, "_BinkPause@8");
	gFuncEntry[49] = GetProcAddress(gOriginalBinkW32, "_BinkRegisterFrameBuffers@8");
	gFuncEntry[50] = GetProcAddress(gOriginalBinkW32, "_BinkRequestStopAsyncThread@4");
	gFuncEntry[51] = GetProcAddress(gOriginalBinkW32, "_BinkRestoreCursor@4");
	gFuncEntry[52] = GetProcAddress(gOriginalBinkW32, "_BinkService@4");
	gFuncEntry[53] = GetProcAddress(gOriginalBinkW32, "_BinkSetError@4");
	gFuncEntry[54] = GetProcAddress(gOriginalBinkW32, "_BinkSetFrameRate@8");
	gFuncEntry[55] = GetProcAddress(gOriginalBinkW32, "_BinkSetIO@4");
	gFuncEntry[56] = GetProcAddress(gOriginalBinkW32, "_BinkSetIOSize@4");
	gFuncEntry[57] = GetProcAddress(gOriginalBinkW32, "_BinkSetMemory@8");
	gFuncEntry[58] = GetProcAddress(gOriginalBinkW32, "_BinkSetMixBinVolumes@20");
	gFuncEntry[59] = GetProcAddress(gOriginalBinkW32, "_BinkSetMixBins@16");
	gFuncEntry[60] = GetProcAddress(gOriginalBinkW32, "_BinkSetPan@12");
	gFuncEntry[61] = GetProcAddress(gOriginalBinkW32, "_BinkSetSimulate@4");
	gFuncEntry[62] = GetProcAddress(gOriginalBinkW32, "_BinkSetSoundOnOff@8");
	gFuncEntry[63] = GetProcAddress(gOriginalBinkW32, "_BinkSetSoundSystem@8");
	gFuncEntry[64] = GetProcAddress(gOriginalBinkW32, "_BinkSetSoundTrack@8");
	gFuncEntry[65] = GetProcAddress(gOriginalBinkW32, "_BinkSetVideoOnOff@8");
	gFuncEntry[66] = GetProcAddress(gOriginalBinkW32, "_BinkSetVolume@12");
	gFuncEntry[67] = GetProcAddress(gOriginalBinkW32, "_BinkShouldSkip@4");
	gFuncEntry[68] = GetProcAddress(gOriginalBinkW32, "_BinkStartAsyncThread@8");
	gFuncEntry[69] = GetProcAddress(gOriginalBinkW32, "_BinkWait@4");
	gFuncEntry[70] = GetProcAddress(gOriginalBinkW32, "_BinkWaitStopAsyncThread@4");
	gFuncEntry[71] = GetProcAddress(gOriginalBinkW32, "_RADTimerRead@0");
}

// _BinkBufferBlit@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferBlit(int a1, int a2, int a3)
{
	__asm
	{
		jmp gFuncEntry[0 * 4];
	}
}

// _BinkBufferCheckWinPos@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferCheckWinPos(int a1, int a2, int a3)
{
	__asm
	{
		jmp gFuncEntry[1 * 4];
	}
}

// _BinkBufferClear@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferClear(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[2 * 4];
	}
}

// _BinkBufferClose@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferClose(int a1)
{
	__asm
	{
		jmp gFuncEntry[3 * 4];
	}
}

// _BinkBufferGetDescription@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferGetDescription(int a1)
{
	__asm
	{
		jmp gFuncEntry[4 * 4];
	}
}

// _BinkBufferGetError@0
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferGetError()
{
	__asm
	{
		jmp gFuncEntry[5 * 4];
	}
}

// _BinkBufferLock@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferLock(int a1)
{
	__asm
	{
		jmp gFuncEntry[6 * 4];
	}
}

// _BinkBufferOpen@16
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferOpen(int a1, int a2, int a3, int a4)
{
	__asm
	{
		jmp gFuncEntry[7 * 4];
	}
}

// _BinkBufferSetDirectDraw@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferSetDirectDraw(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[8 * 4];
	}
}

// _BinkBufferSetHWND@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferSetHWND(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[9 * 4];
	}
}

// _BinkBufferSetOffset@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferSetOffset(int a1, int a2, int a3)
{
	__asm
	{
		jmp gFuncEntry[10 * 4];
	}
}

// _BinkBufferSetResolution@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferSetResolution(int a1, int a2, int a3)
{
	__asm
	{
		jmp gFuncEntry[11 * 4];
	}
}

// _BinkBufferSetScale@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferSetScale(int a1, int a2, int a3)
{
	__asm
	{
		jmp gFuncEntry[12 * 4];
	}
}

// _BinkBufferUnlock@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkBufferUnlock(int a1)
{
	__asm
	{
		jmp gFuncEntry[13 * 4];
	}
}

// _BinkCheckCursor@20
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkCheckCursor(int a1, int a2, int a3, int a4, int a5)
{
	__asm
	{
		jmp gFuncEntry[14 * 4];
	}
}

// _BinkClose@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkClose(int a1)
{
	__asm
	{
		jmp gFuncEntry[15 * 4];
	}
}

// _BinkCloseTrack@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkCloseTrack(int a1)
{
	__asm
	{
		jmp gFuncEntry[16 * 4];
	}
}

// _BinkControlBackgroundIO@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkControlBackgroundIO(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[17 * 4];
	}
}

// _BinkControlPlatformFeatures@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkControlPlatformFeatures(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[18 * 4];
	}
}

// _BinkCopyToBuffer@28
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkCopyToBuffer(int a1, int a2, int a3, int a4, int a5, int a6, int a7)
{
	__asm
	{
		jmp gFuncEntry[19 * 4];
	}
}

// _BinkCopyToBufferRect@44
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkCopyToBufferRect(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11)
{
	__asm
	{
		jmp gFuncEntry[20 * 4];
	}
}

// _BinkDDSurfaceType@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkDDSurfaceType(int a1)
{
	__asm
	{
		jmp gFuncEntry[21 * 4];
	}
}

// _BinkDX8SurfaceType@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkDX8SurfaceType(int a1)
{
	__asm
	{
		jmp gFuncEntry[22 * 4];
	}
}

// _BinkDX9SurfaceType@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkDX9SurfaceType(int a1)
{
	__asm
	{
		jmp gFuncEntry[23 * 4];
	}
}

// _BinkDoFrame@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkDoFrame(int a1)
{
	__asm
	{
		jmp gFuncEntry[24 * 4];
	}
}

// _BinkDoFrameAsync@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkDoFrameAsync(int a1, int a2, int a3)
{
	__asm
	{
		jmp gFuncEntry[25 * 4];
	}
}

// _BinkDoFrameAsyncWait@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkDoFrameAsyncWait(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[26 * 4];
	}
}

// _BinkDoFramePlane@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkDoFramePlane(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[27 * 4];
	}
}

// _BinkGetError@0
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetError()
{
	__asm
	{
		jmp gFuncEntry[28 * 4];
	}
}

// _BinkGetFrameBuffersInfo@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetFrameBuffersInfo(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[29 * 4];
	}
}

// _BinkGetKeyFrame@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetKeyFrame(int a1, int a2, int a3)
{
	__asm
	{
		jmp gFuncEntry[30 * 4];
	}
}

// _BinkGetPalette@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetPalette(int a1)
{
	__asm
	{
		jmp gFuncEntry[31 * 4];
	}
}

// _BinkGetRealtime@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetRealtime(int a1, int a2, int a3)
{
	__asm
	{
		jmp gFuncEntry[32 * 4];
	}
}

// _BinkGetRects@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetRects(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[33 * 4];
	}
}

// _BinkGetSummary@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetSummary(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[34 * 4];
	}
}

// _BinkGetTrackData@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetTrackData(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[35 * 4];
	}
}

// _BinkGetTrackID@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetTrackID(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[36 * 4];
	}
}

// _BinkGetTrackMaxSize@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetTrackMaxSize(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[37 * 4];
	}
}

// _BinkGetTrackType@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGetTrackType(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[38 * 4];
	}
}

// _BinkGoto@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkGoto(int a1, int a2, int a3)
{
	__asm
	{
		jmp gFuncEntry[39 * 4];
	}
}

// _BinkIsSoftwareCursor@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkIsSoftwareCursor(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[40 * 4];
	}
}

// _BinkLogoAddress@0
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkLogoAddress()
{
	__asm
	{
		jmp gFuncEntry[41 * 4];
	}
}

// _BinkNextFrame@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkNextFrame(int a1)
{
	__asm
	{
		jmp gFuncEntry[42 * 4];
	}
}

// _BinkOpen@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkOpen(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[43 * 4];
	}
}

// _BinkOpenDirectSound@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkOpenDirectSound(int a1)
{
	__asm
	{
		jmp gFuncEntry[44 * 4];
	}
}

// _BinkOpenMiles@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkOpenMiles(int a1)
{
	__asm
	{
		jmp gFuncEntry[45 * 4];
	}
}

// _BinkOpenTrack@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkOpenTrack(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[46 * 4];
	}
}

// _BinkOpenWaveOut@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkOpenWaveOut(int a1)
{
	__asm
	{
		jmp gFuncEntry[47 * 4];
	}
}

// _BinkPause@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkPause(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[48 * 4];
	}
}

// _BinkRegisterFrameBuffers@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkRegisterFrameBuffers(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[49 * 4];
	}
}

// _BinkRequestStopAsyncThread@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkRequestStopAsyncThread(int a1)
{
	__asm
	{
		jmp gFuncEntry[50 * 4];
	}
}

// _BinkRestoreCursor@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkRestoreCursor(int a1)
{
	__asm
	{
		jmp gFuncEntry[51 * 4];
	}
}

// _BinkService@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkService(int a1)
{
	__asm
	{
		jmp gFuncEntry[52 * 4];
	}
}

// _BinkSetError@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetError(int a1)
{
	__asm
	{
		jmp gFuncEntry[53 * 4];
	}
}

// _BinkSetFrameRate@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetFrameRate(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[54 * 4];
	}
}

// _BinkSetIO@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetIO(int a1)
{
	__asm
	{
		jmp gFuncEntry[55 * 4];
	}
}

// _BinkSetIOSize@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetIOSize(int a1)
{
	__asm
	{
		jmp gFuncEntry[56 * 4];
	}
}

// _BinkSetMemory@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetMemory(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[57 * 4];
	}
}

// _BinkSetMixBinVolumes@20
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetMixBinVolumes(int a1, int a2, int a3, int a4, int a5)
{
	__asm
	{
		jmp gFuncEntry[58 * 4];
	}
}

// _BinkSetMixBins@16
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetMixBins(int a1, int a2, int a3, int a4)
{
	__asm
	{
		jmp gFuncEntry[59 * 4];
	}
}

// _BinkSetPan@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetPan(int a1, int a2, int a3)
{
	__asm
	{
		jmp gFuncEntry[60 * 4];
	}
}

// _BinkSetSimulate@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetSimulate(int a1)
{
	__asm
	{
		jmp gFuncEntry[61 * 4];
	}
}

// _BinkSetSoundOnOff@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetSoundOnOff(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[62 * 4];
	}
}

// _BinkSetSoundSystem@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetSoundSystem(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[63 * 4];
	}
}

// _BinkSetSoundTrack@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetSoundTrack(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[64 * 4];
	}
}

// _BinkSetVideoOnOff@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetVideoOnOff(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[65 * 4];
	}
}

// _BinkSetVolume@12
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkSetVolume(int a1, int a2, int a3)
{
	__asm
	{
		jmp gFuncEntry[66 * 4];
	}
}

// _BinkShouldSkip@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkShouldSkip(int a1)
{
	__asm
	{
		jmp gFuncEntry[67 * 4];
	}
}

// _BinkStartAsyncThread@8
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkStartAsyncThread(int a1, int a2)
{
	__asm
	{
		jmp gFuncEntry[68 * 4];
	}
}

// _BinkWait@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkWait(int a1)
{
	__asm
	{
		jmp gFuncEntry[69 * 4];
	}
}

// _BinkWaitStopAsyncThread@4
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall BinkWaitStopAsyncThread(int a1)
{
	__asm
	{
		jmp gFuncEntry[70 * 4];
	}
}

// _RADTimerRead@0
extern "C" __declspec(dllexport) __declspec(naked) void __stdcall RADTimerRead()
{
	__asm
	{
		jmp gFuncEntry[71 * 4];
	}
}

#pragma warning(pop)
