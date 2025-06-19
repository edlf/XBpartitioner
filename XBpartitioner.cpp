// XBpartitioner.cpp : Defines the entry point for the application.
#include "stdafx.h"
#include "xbpartitioner.h"
#include "partition.hpp"

#include <xfont.h> //used for directX textout
#include <io.h>
#include <stdio.h> //used for swprintf
#include <stdlib.h>
#include <Xtl.h>
#include <winbase.h>
#include "Winioctl.h"
#include "Undocumented.h"
// Fonts
#include "arial18normal.h"
#include "lucon18bold.h"
#include "arial32bold.h"

#include "prometheos_code.h"

void App::InitialiseD3D()
{
    // First of all, create the main D3D object. If it is created successfully we
    // should get a pointer to an IDirect3D8 interface.
    g_pD3D = Direct3DCreate8(D3D_SDK_VERSION);

    // Create a structure to hold the settings for our device
    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));

    // Fill the structure.
    // Set fullscreen 640x480x32 mode
    d3dpp.BackBufferWidth = SCREEN_SIZE_X;
    d3dpp.BackBufferHeight = SCREEN_SIZE_Y;
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;

    // Create one backbuffer and a z-buffer
    d3dpp.BackBufferCount = 1;

    // Set up how the backbuffer is "presented" to the frontbuffer each time
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;

    // Create a Direct3D device.
    g_pD3D->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
                         D3DCREATE_HARDWARE_VERTEXPROCESSING,
                         &d3dpp, &g_pD3DDevice);

    g_pD3DDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_pD3DDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    DWORD videoFlags = XGetVideoFlags();

    // Turn on z-buffering
    g_pD3DDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);

    // Fonts
    XFONT_OpenBitmapFontFromMemory(lucon18bold, sizeof(lucon18bold), &g_pLucon18BitmapFont);
    XFONT_OpenBitmapFontFromMemory(arial18normal, sizeof(arial18normal), &g_pArial18BitmapFont);
    XFONT_OpenBitmapFontFromMemory(arial32bold, sizeof(arial32bold), &g_pArial32BitmapFont);
}

void App::CleanUpD3D()
{
    g_pLucon18BitmapFont->Release();
    g_pArial18BitmapFont->Release();
    g_pArial32BitmapFont->Release();
    g_pD3DDevice->Release();
    g_pD3D->Release();
}

void App::DrawRect(FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, DWORD dwStartColor, DWORD dwEndColor)
{
    SCREENVERTEX v[4];
    v[0].pos = D3DXVECTOR4(x1 - 0.5f, y1 - 0.5f, 1.0f, 1.0f);
    v[0].color = dwStartColor;
    v[1].pos = D3DXVECTOR4(x2 - 0.5f, y1 - 0.5f, 1.0f, 1.0f);
    v[1].color = dwStartColor;
    v[2].pos = D3DXVECTOR4(x1 - 0.5f, y2 - 0.5f, 1.0f, 1.0f);
    v[2].color = dwEndColor;
    v[3].pos = D3DXVECTOR4(x2 - 0.5f, y2 - 0.5f, 1.0f, 1.0f);
    v[3].color = dwEndColor;

    // Render the rectangle
    g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
    g_pD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    g_pD3DDevice->SetVertexShader(D3DFVF_SCREENVERTEX);
    g_pD3DDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(SCREENVERTEX));
}

void App::DrawText(XFONT *font, LPCWSTR text_string, ULONG xpos, ULONG ypos, Color color, int just)
{
    // Create some DirectX text buffers
    LPDIRECT3DSURFACE8 pFrontBuffer;

    // InitialiseFonts
    g_pD3DDevice->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pFrontBuffer);

    // Display our text.
    switch (color)
    {
    case COLOR_WHITE:
        font->SetBkMode(XFONT_TRANSPARENT);
        font->SetTextColor(D3DCOLOR_XRGB(255, 255, 255));
        break;
    case COLOR_RED:
        font->SetBkMode(XFONT_TRANSPARENT);
        font->SetTextColor(D3DCOLOR_XRGB(255, 40, 40));
        break;
    case COLOR_GREEN:
        font->SetBkMode(XFONT_TRANSPARENT);
        font->SetTextColor(D3DCOLOR_XRGB(20, 255, 20));
        break;
    case COLOR_BLUE:
        font->SetBkMode(XFONT_TRANSPARENT);
        font->SetTextColor(D3DCOLOR_XRGB(100, 100, 255));
        break;
    case COLOR_YELLOW:
        font->SetBkMode(XFONT_TRANSPARENT);
        font->SetTextColor(D3DCOLOR_XRGB(224, 224, 20));
        break;
    case COLOR_WHITE_BG:
        font->SetBkMode(XFONT_OPAQUE);
        font->SetBkColor(D3DCOLOR_XRGB(40, 40, 40));
        font->SetTextColor(D3DCOLOR_XRGB(255, 255, 255));
        break;
    case COLOR_RED_BG:
        font->SetBkMode(XFONT_OPAQUE);
        font->SetBkColor(D3DCOLOR_XRGB(40, 40, 40));
        font->SetTextColor(D3DCOLOR_XRGB(255, 20, 20));
        break;
    case COLOR_GREY:
        font->SetBkMode(XFONT_TRANSPARENT);
        font->SetTextColor(D3DCOLOR_XRGB(140, 140, 140));
        break;
    case COLOR_DARK_YELLOW:
        font->SetBkMode(XFONT_TRANSPARENT);
        font->SetTextColor(D3DCOLOR_XRGB(160, 160, 0));
        break;
    default:
        font->SetBkMode(XFONT_TRANSPARENT);
        font->SetTextColor(D3DCOLOR_XRGB(240, 240, 240));
        break;
    }

    font->SetTextAlignment(XFONT_TOP | just);
    font->TextOut(pFrontBuffer, text_string, -1, (long)xpos, (long)ypos);
    pFrontBuffer->Release();
}

void App::DisplayText(LPCWSTR text_string, ULONG xpos, ULONG ypos, Color color, int just)
{
    DrawText(g_pArial18BitmapFont, text_string, xpos, ypos, color, just);
}

void App::DisplayTableText(LPCWSTR text_string, ULONG xpos, ULONG ypos, Color color, int just)
{
    DrawText(g_pLucon18BitmapFont, text_string, xpos, ypos, color, just);
}

void App::DisplayWarningBackupAvailable()
{
    // Create some DirectX text buffers
    LPDIRECT3DSURFACE8 pFrontBuffer;

    // InitialiseFonts
    g_pD3DDevice->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pFrontBuffer);

    // Display our text.
    g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    g_pArial32BitmapFont->SetTextAlignment(XFONT_CENTER | XFONT_TOP);
    g_pArial32BitmapFont->SetTextColor(D3DCOLOR_XRGB(255, 30, 20));
    g_pArial32BitmapFont->TextOut(pFrontBuffer, L"!!!!!!!! WARNING !!!!!!!!", -1, 320, 80);

    g_pArial18BitmapFont->SetTextColor(D3DCOLOR_XRGB(240, 240, 240));
    g_pArial18BitmapFont->SetTextAlignment(XFONT_LEFT | XFONT_TOP);

    g_pArial18BitmapFont->TextOut(pFrontBuffer, L"Main partition table is missing, however backup is available", -1, 60, 200);
    g_pArial18BitmapFont->TextOut(pFrontBuffer, L"Press Y twice to load the backup table.", -1, 60, 240);
    g_pArial18BitmapFont->TextOut(pFrontBuffer, L"Press any key to continue...", -1, 60, 280);

    g_pD3DDevice->Present(NULL, NULL, NULL, NULL);

    pFrontBuffer->Release();
}

void App::DisplayWarning()
{
    // Create some DirectX text buffers
    LPDIRECT3DSURFACE8 pFrontBuffer;

    // InitialiseFonts
    g_pD3DDevice->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pFrontBuffer);

    // Display our text.
    g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    g_pArial32BitmapFont->SetTextAlignment(XFONT_CENTER | XFONT_TOP);
    g_pArial32BitmapFont->SetTextColor(D3DCOLOR_XRGB(255, 30, 20));
    g_pArial32BitmapFont->TextOut(pFrontBuffer, L"!!!!!!!! WARNING !!!!!!!!", -1, 320, 80);

    g_pArial18BitmapFont->SetTextColor(D3DCOLOR_XRGB(240, 240, 240));
    g_pArial18BitmapFont->SetTextAlignment(XFONT_LEFT | XFONT_TOP);

    if (g_iFunction == FUNCTION_WRITE_TABLE_FORMAT)
    {
        g_pArial18BitmapFont->TextOut(pFrontBuffer, L"Changed partitions will be formatted!", -1, 60, 200);
    }
    else
    {
        wchar_t format_str[50];
        swprintf(format_str, L"Partition %d will be formatted!", g_iCurrentPart + 1);
        g_pArial18BitmapFont->TextOut(pFrontBuffer, format_str, -1, 60, 200);
    }

    g_pArial18BitmapFont->TextOut(pFrontBuffer, L"Press", -1, 60, 240);
    g_pArial18BitmapFont->SetTextColor(D3DCOLOR_XRGB(240, 240, 0));
    g_pArial18BitmapFont->TextOut(pFrontBuffer, L"Y", -1, 110, 240);
    g_pArial18BitmapFont->SetTextColor(D3DCOLOR_XRGB(240, 240, 240));
    g_pArial18BitmapFont->TextOut(pFrontBuffer, L"to continue or any other button to abort.", -1, 128, 240);

    g_pD3DDevice->Present(NULL, NULL, NULL, NULL);

    pFrontBuffer->Release();
}

bool App::DisplayNoInfo()
{
    LD_LAUNCH_DASHBOARD LaunchDashData = {XLD_LAUNCH_DASHBOARD_MAIN_MENU};

    // Create some DirectX text buffers
    LPDIRECT3DSURFACE8 pFrontBuffer;

    // InitialiseFonts
    g_pD3DDevice->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pFrontBuffer);

    // Display our text.
    g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    g_pD3DDevice->Present(NULL, NULL, NULL, NULL);
    g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

    g_pArial32BitmapFont->SetTextAlignment(XFONT_CENTER | XFONT_TOP);
    g_pArial32BitmapFont->SetTextColor(D3DCOLOR_XRGB(255, 30, 20));
    g_pArial32BitmapFont->TextOut(pFrontBuffer, L"!!!!!!!! WARNING !!!!!!!!", -1, 320, 80);

    g_pArial18BitmapFont->SetTextColor(D3DCOLOR_XRGB(240, 240, 240));
    g_pArial18BitmapFont->SetTextAlignment(XFONT_LEFT | XFONT_TOP);

    g_pArial18BitmapFont->TextOut(pFrontBuffer, L"LBA48 info not found! Custom Partitions will not work with this BIOS!", -1, 50, 200);
    g_pArial18BitmapFont->TextOut(pFrontBuffer, L"Update your BIOS to Cerbios.", -1, 50, 240);
    g_pArial18BitmapFont->TextOut(pFrontBuffer, L"Press", -1, 50, 280);
    g_pArial18BitmapFont->SetTextColor(D3DCOLOR_XRGB(240, 240, 0));
    g_pArial18BitmapFont->TextOut(pFrontBuffer, L"Y", -1, 100, 280);
    g_pArial18BitmapFont->SetTextColor(D3DCOLOR_XRGB(240, 240, 240));
    g_pArial18BitmapFont->TextOut(pFrontBuffer, L"to continue or any other button to abort.", -1, 118, 280);

    // Release our TextBuffers
    pFrontBuffer->Release();

    g_pD3DDevice->Present(NULL, NULL, NULL, NULL);

    if (-1 != XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY, 0))
    {
        XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);
    }

    XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);

    if (aButton == 3)
    {
        return 1;
    }

    return 0;
}

bool App::format_partition(const int i)
{
    const int cluster_size = tableManipulator->CalculateClusterSizeForPartition(i);
    const ULONG cluster_size_bytes = cluster_size << 10;

    BOOL status = XapiFormatFATVolumeEx(&partition_str[i], cluster_size_bytes);

    wchar_t format_str[50];

    // XapiFormatFATVolumeEx returns a BOOL (TRUE on success), not an NTSTATUS
    if (!status)
    {
        swprintf(format_str, L"Failed to format partition...");
        g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
        DisplayText(format_str, 60, 428, COLOR_RED);
        DisplayText(L"Press any button to continue.", 60, 448, COLOR_WHITE);
        g_pD3DDevice->Present(NULL, NULL, NULL, NULL);
        XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);
        return false;
    }

    bool res = largePartitionFixup(&partition_str[i], tableManipulator->getPartitionEntry(i), cluster_size_bytes);

    if (!res)
    {
        swprintf(format_str, L"Failed to fix up big partition...");
        g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
        DisplayText(format_str, 60, 428, COLOR_RED);
        DisplayText(L"Press any button to continue.", 60, 448, COLOR_WHITE);
        g_pD3DDevice->Present(NULL, NULL, NULL, NULL);
        XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);
        return false;
    }

    return true;
}

ULONGLONG App::get_hdd_size()
{
    ANSI_STRING a_file;
    RtlInitAnsiString(&a_file, "\\Device\\Harddisk0\\partition0");

    HANDLE handle;
    OBJECT_ATTRIBUTES obj_attr;
    obj_attr.RootDirectory = 0;
    obj_attr.ObjectName = &a_file;
    obj_attr.Attributes = OBJ_CASE_INSENSITIVE;

    IO_STATUS_BLOCK io_stat_block;
    unsigned int stat = NtOpenFile(&handle, (GENERIC_READ | 0x00100000),
                                   &obj_attr, &io_stat_block, (FILE_SHARE_READ | FILE_SHARE_WRITE), 0x10);

    if (stat != STATUS_SUCCESS)
    {
        return stat;
    }

    unsigned int geom_ioctl_cmd_in_buf[100];
    memset(geom_ioctl_cmd_in_buf, 0, sizeof(geom_ioctl_cmd_in_buf));
    geom_ioctl_cmd_in_buf[0] = IOCTL_SUBCMD_GET_INFO;

    DISK_GEOMETRY DiskGeometry;
    stat = NtDeviceIoControlFile(handle, 0, 0, 0, &io_stat_block,
                                 IOCTL_DISK_GET_DRIVE_GEOMETRY,
                                 geom_ioctl_cmd_in_buf, sizeof(geom_ioctl_cmd_in_buf),
                                 &DiskGeometry, sizeof(DiskGeometry));
    NtClose(handle);

    ULONGLONG total_sectors = 0ULL;
    if (stat == STATUS_SUCCESS)
    {
        total_sectors = DiskGeometry.Cylinders.QuadPart;
    }

    return total_sectors;
}

bool App::read_lba48_info(XboxPartitionTable *p_table)
{
    ANSI_STRING a_file;
    RtlInitAnsiString(&a_file, "\\Device\\Harddisk0\\partition0");

    HANDLE handle;
    OBJECT_ATTRIBUTES obj_attr;
    obj_attr.RootDirectory = 0;
    obj_attr.ObjectName = &a_file;
    obj_attr.Attributes = OBJ_CASE_INSENSITIVE;

    IO_STATUS_BLOCK io_stat_block;
    unsigned int stat = NtOpenFile(&handle, (GENERIC_READ | 0x00100000),
                                   &obj_attr, &io_stat_block, (FILE_SHARE_READ | FILE_SHARE_WRITE), 0x10);

    if (stat != STATUS_SUCCESS)
    {
        return false;
    }

    memset(p_table, 0, sizeof(XboxPartitionTable));

    unsigned int lba48_ioctl_cmd_in_buf[100];
    unsigned int lba48_ioctl_cmd_out_buf[100];

    memset(lba48_ioctl_cmd_in_buf, 0, sizeof(lba48_ioctl_cmd_in_buf));
    lba48_ioctl_cmd_in_buf[0] = IOCTL_SUBCMD_GET_INFO;

    memset(lba48_ioctl_cmd_out_buf, 0, sizeof(lba48_ioctl_cmd_out_buf));

    stat = NtDeviceIoControlFile(handle, 0, 0, 0, &io_stat_block,
                                 IOCTL_CMD_LBA48_ACCESS,
                                 lba48_ioctl_cmd_in_buf, sizeof(lba48_ioctl_cmd_in_buf),
                                 lba48_ioctl_cmd_out_buf, sizeof(lba48_ioctl_cmd_out_buf));

    NtClose(handle);
    if (stat != STATUS_SUCCESS)
    {
        return false;
    }

    if ((lba48_ioctl_cmd_out_buf[LBA48_GET_INFO_MAGIC1_IDX] != LBA48_GET_INFO_MAGIC1_VAL) ||
        (lba48_ioctl_cmd_out_buf[LBA48_GET_INFO_MAGIC2_IDX] != LBA48_GET_INFO_MAGIC2_VAL))
    {
        return false;
    }

    // this way is compatible with Paul's lba48 patched kernels
    unsigned int partition_table_addr = lba48_ioctl_cmd_out_buf[LBA48_GET_INFO_LOWCODE_BASE_IDX];
    partition_table_addr += lba48_ioctl_cmd_out_buf[LBA48_GET_INFO_PART_TABLE_OFS_IDX];

    g_lba48_version = lba48_ioctl_cmd_out_buf[LBA48_GET_INFO_PATCHCODE_VERSION_IDX];
    g_full_lba48 = (g_lba48_version > 3);

    if (g_lba48_version == 2)
    {
        partition_table_addr += 0x200;
    }

    KernelPartTblAdd = (XboxPartitionTable *)partition_table_addr;

    // Read partition table from ram?
    memcpy(p_table, (void *)partition_table_addr, sizeof(XboxPartitionTable));

    return true;
}

bool App::reload_lba48()
{
    if (tableManipulator != NULL)
    {
        delete (tableManipulator);
        tableManipulator = NULL;
    }

    if (tableManipulatorOriginal != NULL)
    {
        delete (tableManipulatorOriginal);
        tableManipulatorOriginal = NULL;
    }

    // Read kernel patch info + kernel reported partition table
    if (read_lba48_info(&tableKernel))
    {
        tableManipulator = new XboxPartitionTableManipulator(tableKernel, g_TotalHDDSectors, g_full_lba48);
        tableManipulatorOriginal = new XboxPartitionTableManipulator(tableKernel, g_TotalHDDSectors, g_full_lba48);
        return true;
    }
    else
    {
        tableManipulator = new XboxPartitionTableManipulator(g_TotalHDDSectors, g_full_lba48);
        tableManipulatorOriginal = new XboxPartitionTableManipulator(g_TotalHDDSectors, g_full_lba48);
        return false;
    }
}

unsigned int App::get_cluster_size_from_partition(ANSI_STRING part)
{
    HANDLE hDisk;
    NTSTATUS status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    BYTE PartSector[HDD_SECTOR_SIZE * 8];
    LARGE_INTEGER RWOffset;
    FATX_SUPERBLOCK fsb;

    InitializeObjectAttributes(&ObjectAttributes, &part, OBJ_CASE_INSENSITIVE, NULL);

    RWOffset.QuadPart = 0;
    memset(PartSector, 0, sizeof(PartSector));

    status = NtOpenFile(&hDisk, SYNCHRONIZE | GENERIC_ALL, &ObjectAttributes, &IoStatusBlock, 0, FILE_SYNCHRONOUS_IO_ALERT);

    if (NT_SUCCESS(status))
    {
        status = NtReadFile(hDisk, NULL, NULL, NULL, &IoStatusBlock, (PVOID)PartSector, sizeof(PartSector), &RWOffset);
    }
    else
    {
        return 0;
    }
    NtClose(hDisk);
    memcpy(&fsb, PartSector, sizeof(PartSector));
    return fsb.ClusterSize / 2;
}

int App::XInputGetStateAny(int *analog, int *analog_val, int *buttons, short *l_thumb_y_val, int wait)
{
    XINPUT_STATE ControllerState[4];
    int i, j, c = -1;
    *analog = -1;
    *buttons = 0;
    *l_thumb_y_val = 0;

    do
    {
        for (i = 0; i < 4; i++)
        {
            if (g_Controller[i])
            {
                XInputGetState(g_Controller[i], &ControllerState[i]);
                *buttons = ControllerState[i].Gamepad.wButtons;

                if (*buttons)
                {
                    c = i;
                    break;
                }

                if (!wait && (ControllerState[i].Gamepad.sThumbLY < -28672 || ControllerState[i].Gamepad.sThumbLY > 28671))
                {
                    *l_thumb_y_val = ControllerState[i].Gamepad.sThumbLY;
                    c = i;
                    break;
                }

                for (j = 0; j < 8; j++)
                {
                    if (ControllerState[i].Gamepad.bAnalogButtons[j])
                    {
                        *analog_val = ControllerState[i].Gamepad.bAnalogButtons[j];
                        *analog = j;
                        c = i;
                        break;
                    }
                }
            }
        }
        if (!wait)
        {
            return c;
        }
    } while (c == -1);

    for (; wait == 2;)
    {
        XInputGetState(g_Controller[c], &ControllerState[c]);

        if (!ControllerState[c].Gamepad.wButtons && !ControllerState[c].Gamepad.bAnalogButtons[*analog])
        {
            break;
        }
    }

    return c;
}

bool App::CheckIfPartitionTableChanged()
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        ULONGLONG new_partition_start = tableManipulator->GetPartitionStart(i);
        ULONGLONG new_partition_size = tableManipulator->GetPartitionSize(i);

        ULONGLONG existing_partition_start = tableManipulatorOriginal->GetPartitionStart(i);
        ULONGLONG existing_partition_size = tableManipulatorOriginal->GetPartitionSize(i);

        // Check if partition actually changed
        if (tableManipulator->isPartitionEnabled(i) && (existing_partition_size != new_partition_size || existing_partition_start != new_partition_start))
        {
            return true;
        }
    }
    return false;
}

LARGE_INTEGER App::get_backup_partition_offset_bytes()
{
    LARGE_INTEGER byte_pos;
    byte_pos.QuadPart = (ULONGLONG)(g_TotalHDDSectors - RESERVED_SPACE_END_LBA) * (ULONGLONG)HDD_SECTOR_SIZE;
    return byte_pos;
}

bool App::write_partition_tables(const XboxPartitionTable table)
{
    LARGE_INTEGER RWOffset;
    RWOffset.QuadPart = 0;
    LARGE_INTEGER RWOffset_backup = get_backup_partition_offset_bytes();

    BYTE PartSector[HDD_SECTOR_SIZE]; // XboxPartitionTable should be the same size as HDD_SECTOR_SIZE
    memcpy(PartSector, &table, HDD_SECTOR_SIZE);

    HANDLE hDisk;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ANSI_STRING a_path;
    RtlInitAnsiString(&a_path, "\\Device\\Harddisk0\\Partition0");
    InitializeObjectAttributes(&ObjectAttributes, &a_path, OBJ_CASE_INSENSITIVE, NULL);
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS status = NtOpenFile(&hDisk, SYNCHRONIZE | GENERIC_ALL, &ObjectAttributes, &IoStatusBlock, 0, FILE_SYNCHRONOUS_IO_ALERT);

    if (status == 0)
    {
        // Write main table
        status = NtWriteFile(hDisk, NULL, NULL, NULL, &IoStatusBlock, (PVOID)PartSector, HDD_SECTOR_SIZE, &RWOffset);
    }

    if (status == 0)
    {
        // Write backup table
        status = NtWriteFile(hDisk, NULL, NULL, NULL, &IoStatusBlock, (PVOID)PartSector, HDD_SECTOR_SIZE, &RWOffset_backup);
    }

    // This shouldn't really be here, but both the kernel and the internal XBpartitioner logic need to know about the new partition table
    if (status == 0 && KernelPartTblAdd)
    {
        // p_table_add
        memcpy(KernelPartTblAdd, &table, sizeof(XboxPartitionTable));
        memcpy(&tableKernel, &table, sizeof(XboxPartitionTable));
        if (tableManipulator)
        {
            tableManipulator->LoadNewTable(table);
        }
    }

    NtClose(hDisk);

    if (status == 0)
    {
        return reload_lba48();
    }

    return false;
}

bool App::erase_partition_tables()
{
    tableManipulator->SetupPartitionsStock();
    return write_partition_tables(tableManipulator->GetPartitionTable());
}

XboxPartitionTable App::read_hdd_partition_table(LARGE_INTEGER offset)
{
    XboxPartitionTable table;
    LARGE_INTEGER RWOffset = offset;

    HANDLE hDisk;
    NTSTATUS status;
    ANSI_STRING a_path;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    BYTE PartSector[HDD_SECTOR_SIZE];

    RtlInitAnsiString(&a_path, "\\Device\\Harddisk0\\Partition0");
    InitializeObjectAttributes(&ObjectAttributes, &a_path, OBJ_CASE_INSENSITIVE, NULL);
    memset(PartSector, 0, sizeof(PartSector));

    status = NtOpenFile(&hDisk, SYNCHRONIZE | GENERIC_ALL, &ObjectAttributes, &IoStatusBlock, 0, FILE_SYNCHRONOUS_IO_ALERT);
    if (status == 0)
    {
        status = NtReadFile(hDisk, NULL, NULL, NULL, &IoStatusBlock, (PVOID)PartSector, HDD_SECTOR_SIZE, &RWOffset);
    }
    if (status == 0)
    {
        memcpy(&table, PartSector, HDD_SECTOR_SIZE);
    }
    NtClose(hDisk);
    return table;
}

XboxPartitionTable App::read_hdd_main_partition_table()
{
    XboxPartitionTable table;

    if (g_TotalHDDSectors > 0)
    {
        LARGE_INTEGER offset;
        offset.QuadPart = 0;
        table = read_hdd_partition_table(offset);
    }

    return table;
}

XboxPartitionTable App::read_hdd_backup_partition_table()
{
    XboxPartitionTable table;

    if (g_TotalHDDSectors > 0)
    {
        table = read_hdd_partition_table(get_backup_partition_offset_bytes());
    }

    return table;
}

void App::DisplayHelp()
{
    // Create some DirectX text buffers
    LPDIRECT3DSURFACE8 m_pFrontBuffer, m_pBackBuffer;
    g_pD3DDevice->GetBackBuffer(-1, D3DBACKBUFFER_TYPE_MONO, &m_pFrontBuffer);
    g_pD3DDevice->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &m_pBackBuffer);

    LD_LAUNCH_DASHBOARD LaunchDashData = {XLD_LAUNCH_DASHBOARD_MAIN_MENU};

    g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

    int ypos = 1;

    DisplayText(L"XBpartitioner Help", 319, 20 * ypos++, COLOR_WHITE, XFONT_CENTER);
    ypos++;
    DisplayText(L"A...................................", 50, 20 * ypos, COLOR_GREEN);
    DisplayText(L"cycle through standard setup schemes", 240, 20 * ypos++, COLOR_WHITE);
    DisplayText(L"B...................................", 50, 20 * ypos, COLOR_RED);
    DisplayText(L"does nothing", 240, 20 * ypos++, COLOR_WHITE);
    DisplayText(L"X...................................", 50, 20 * ypos, COLOR_BLUE);
    DisplayText(L"toggle change size \\ change start sector", 240, 20 * ypos++, COLOR_WHITE);
    DisplayText(L"Y...................................", 50, 20 * ypos, COLOR_YELLOW);
    DisplayText(L"cycle kernel\\hdd\\hdd backup partition tables", 240, 20 * ypos++, COLOR_WHITE);
    DisplayText(L"L Trigger.......................", 50, 20 * ypos, COLOR_YELLOW);
    DisplayText(L"decrease partition size", 240, 20 * ypos++, COLOR_WHITE);
    DisplayText(L"R Trigger.......................", 50, 20 * ypos, COLOR_GREEN);
    DisplayText(L"increase partition size", 240, 20 * ypos++, COLOR_WHITE);
    DisplayText(L"D-Pad-Left....................", 50, 20 * ypos, COLOR_RED);
    DisplayText(L"increase partition size by 1GB", 240, 20 * ypos++, COLOR_WHITE);
    DisplayText(L"D-Pad-Right..................", 50, 20 * ypos, COLOR_BLUE);
    DisplayText(L"decrease partition size by 1GB", 240, 20 * ypos++, COLOR_WHITE);
    DisplayText(L"L Thumb Stick...............", 50, 20 * ypos, COLOR_YELLOW);
    DisplayText(L"partition size fine control", 240, 20 * ypos++, COLOR_WHITE);
    DisplayText(L"R Thumb Button............", 50, 20 * ypos, COLOR_GREEN);
    DisplayText(L"enable \\ disable current partition", 240, 20 * ypos++, COLOR_WHITE);
    DisplayText(L"L+R Thumb Buttons......", 50, 20 * ypos, COLOR_RED);
    DisplayText(L"undo all changes since last write", 240, 20 * ypos++, COLOR_WHITE);
    DisplayText(L"Black.............................", 50, 20 * ypos, COLOR_BLUE);
    DisplayText(L"erase the partition table from the disk", 240, 20 * ypos++, COLOR_WHITE);
    DisplayText(L"White.............................", 50, 20 * ypos, COLOR_YELLOW);
    DisplayText(L"select Start Button action", 240, 20 * ypos++, COLOR_WHITE);
    DisplayText(L"Start..............................", 50, 20 * ypos, COLOR_GREEN);
    DisplayText(L"write & format changed \\ write only \\ format", 240, 20 * ypos++, COLOR_WHITE);
    ypos++;
    DisplayText(L"Actions which write to disk require confirmation.", 50, 4 + 20 * ypos++, COLOR_YELLOW);
    DisplayText(L"Press Back to exit XBpartitioner, any other button to go back.", 50, 4 + 20 * ypos++, COLOR_YELLOW);

    g_pD3DDevice->Present(NULL, NULL, NULL, NULL);

    XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);
    XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);

    if (tButton & XINPUT_GAMEPAD_BACK)
    {
        for (ULONG rect_color = 0x00000000; rect_color != 0xfe000000; rect_color += 0x02000000)
        {
            g_pD3DDevice->CopyRects(m_pFrontBuffer, 0, 0, m_pBackBuffer, 0);
            DrawRect(0, 0, 639, 479, rect_color, rect_color);
            g_pD3DDevice->Present(NULL, NULL, NULL, NULL);
        }

        DisplayText(L"Quitting...", 50, 20 * ypos++, COLOR_GREEN);
        CleanUpD3D();
        XLaunchNewImage(NULL, (LAUNCH_DATA *)&LaunchDashData);
    }
}

bool App::PreparePartitionInfoStrings(const int partition_number, wchar_t * partno_str, wchar_t * size_string, wchar_t * cluster_string, wchar_t * lba_start_str, wchar_t * lba_end_str)
{
    const ULONGLONG lba_start = tableManipulator->GetPartitionStart(partition_number);
    const ULONGLONG lba_size = tableManipulator->GetPartitionSize(partition_number);

    const ULONG start_low = (ULONG)(lba_start & LBA_LOWER_BITS);
    const ULONG start_high = (WORD)(lba_start >> 32);
    const ULONG size_low = (ULONG)(lba_size & LBA_LOWER_BITS);
    const ULONG size_high = (WORD)(lba_size >> 32);

    swprintf(partno_str, L"%d", partition_number + 1);
    swprintf(lba_start_str, L"%.04X%.08X", start_high, start_low);
    swprintf(lba_end_str, L"%.04X%.08X", size_high, size_low);

    // Use gigabytes for extended partitions
    if (partition_number > 4)
    {
        swprintf(size_string, L"%.2f GB", (double)((ULONGLONG)tableManipulator->GetPartitionSizeInBytes(partition_number) / SCALE_GIGABYTE));
    }
    else
    {
        swprintf(size_string, L"%.2f MB", (double)((ULONGLONG)tableManipulator->GetPartitionSizeInBytes(partition_number) / SCALE_MEGABYTE));
    }

    const int cluster_size = get_cluster_size_from_partition(partition_str[partition_number]);

    if (cluster_size > 0)
    {
        // Use the same rule the formatter uses, so boundaries match exactly
        if ((unsigned int)cluster_size < tableManipulator->CalculateClusterSize(lba_size))
        {
            swprintf(cluster_string, L"ERROR");
        }
        else
        {
            swprintf(cluster_string, L"%dKB", cluster_size);
        }
    }
    else
    {
        swprintf(cluster_string, L"");
    }

    return (cluster_size != 0 && lba_size > 0);
}

// Draws main screen showing the partition table
void App::DrawTable()
{
    int offset = 3;
    int ypos = 4;

    const float table_start_x = 37;
    const float table_end_x = 603;

    DisplayText(L"Standard partitions. Custom sizes here might lead to problems", 40, 56, COLOR_WHITE);
    DrawRect(table_start_x, 80, table_end_x, 183.0, D3DCOLOR_XRGB(0, 55, 55), D3DCOLOR_XRGB(0, 130, 168));

    DisplayText(L"Extended partitions", 40, 188, COLOR_WHITE);
    DrawRect(table_start_x, 210, table_end_x, 392, D3DCOLOR_XRGB(155, 0, 168), D3DCOLOR_XRGB(51, 0, 55));

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        // Push extended partitions position
        if (i == 5)
        {
            offset += 30;
        }

        wchar_t partition_size_display_buffer[20];
        wchar_t cluster_size_display_buffer[10];
        wchar_t partno_str[3];
        wchar_t lba_start_str[16];
        wchar_t lba_end_str[16];
        bool draw_cluster = PreparePartitionInfoStrings(i, &partno_str[0], &partition_size_display_buffer[0], &cluster_size_display_buffer[0], &lba_start_str[0], &lba_end_str[0]);

        const int y_pos = 20 * ypos + offset;

        Color text_color = tableManipulator->isPartitionEnabled(i) ? COLOR_WHITE : COLOR_GREY;
        Color text_color_start = text_color;
        Color text_color_size = text_color;

        if (g_iCurrentPart == i)
        {
            // Selection rectangle
            DrawRect(table_start_x, (float)(y_pos - 3), table_end_x, (float)(y_pos + 20), D3DCOLOR_XRGB(51, 0, 55), D3DCOLOR_XRGB(51, 0, 55));

            // Highlights the current value being edited
            if (g_start_size_selector)
            {
                text_color_start = COLOR_YELLOW;
            }
            else
            {
                text_color_size = COLOR_YELLOW;
            }
        }

        // Index
        DisplayTableText(partno_str, 50, y_pos, text_color);
        // LBA Start
        DisplayTableText(lba_start_str, 84, y_pos, text_color_start);
        // Separator
        DisplayText(L":", 230, y_pos, text_color);
        // LBA Size
        DisplayTableText(lba_end_str, 238, y_pos, text_color_size);
        // Cluster
        if (draw_cluster)
        {
            DisplayTableText(cluster_size_display_buffer, 400, y_pos, text_color);
        }
        // Size in MB/GB
        DisplayTableText(partition_size_display_buffer, 592, y_pos, text_color, XFONT_RIGHT);

        ypos++;
    }
}

void App::DrawAvailableTables()
{
    // TODO
}

void App::DrawInfo()
{
    switch (g_iFunction)
    {
    case FUNCTION_WRITE_TABLE:
        DisplayText(L"Press Start to write partition table. Press Back for help.", 60, 448, COLOR_WHITE);
        break;
    case FUNCTION_WRITE_TABLE_FORMAT:
        DisplayText(L"Press Start write table and format. Press Back for help.", 60, 448, COLOR_WHITE);
        break;
    case FUNCTION_FORMAT_CURRENT_PARTITION:
        DisplayText(L"Press Start to format current partition. Press Back for help.", 60, 448, COLOR_WHITE);
        break;
    default:
        DisplayText(L"Noop. Press Back for help.", 60, 448, COLOR_WHITE);
        break;
    }
}

void App::DrawScreen(int g_iCurrentPart)
{
    g_pD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    g_pD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_pD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    g_pD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
    g_pD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    g_pD3DDevice->SetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB(255, 255, 255, 255));

    // Title
    wchar_t header[100];
    swprintf(header, L"XBpartitioner 1.4 - LBA48 patch v%d", g_lba48_version);
    DisplayText(header, 52, 20, COLOR_WHITE, XFONT_LEFT);

    DrawTable();
    DrawAvailableTables();
}

void App::DrawFreeSpace()
{
    uint64_t usable_lbas = tableManipulator->GetUsableLBAs();
    uint64_t available_lbas = tableManipulator->GetAvailableLBAs();

    wchar_t freespace_str1[150];
    wchar_t freespace_str2[150];

    swprintf(freespace_str1, L"Usable free space: %.0f of %.0f GB", (float)available_lbas / 0x200000, (float)usable_lbas / 0x200000);
    swprintf(freespace_str2, L"Usable LBAs: %llu, available LBAs %llu", usable_lbas, available_lbas);
    DisplayText(freespace_str1, 60, 394, COLOR_WHITE);
    DisplayText(freespace_str2, 60, 414, COLOR_WHITE);
}

void App::DrawFormatCluster()
{
    if (g_iFunction == FUNCTION_FORMAT_CURRENT_PARTITION)
    {
        int cluster_size = tableManipulator->CalculateClusterSizeForPartition(g_iCurrentPart);
        wchar_t cluster_str[50];
        swprintf(cluster_str, L"Will use %d KB clusters", cluster_size);
        DisplayText(cluster_str, 380, 394, COLOR_WHITE);
    }
}

void App::CheckTableStatus()
{
    // Check if we should report that main and backup dont match up
    if (memcmp(&tableHDD, &tableHDDBackup, sizeof(XboxPartitionTable)) != 0) {
        // Mismatch between partition tables
        // Create some DirectX text buffers
        LPDIRECT3DSURFACE8 pFrontBuffer;

        // InitialiseFonts
        g_pD3DDevice->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pFrontBuffer);

        // Display our text.
        g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
        g_pArial32BitmapFont->SetTextAlignment(XFONT_CENTER | XFONT_TOP);
        g_pArial32BitmapFont->SetTextColor(D3DCOLOR_XRGB(255, 30, 20));
        g_pArial32BitmapFont->TextOut(pFrontBuffer, L"!!!!!!!! WARNING !!!!!!!!", -1, 320, 80);

        g_pArial18BitmapFont->SetTextColor(D3DCOLOR_XRGB(240, 240, 240));
        g_pArial18BitmapFont->SetTextAlignment(XFONT_LEFT | XFONT_TOP);

        g_pArial18BitmapFont->TextOut(pFrontBuffer, L"Main and backup partition tables dont match!", -1, 60, 200);

        g_pArial18BitmapFont->TextOut(pFrontBuffer, L"Press", -1, 60, 240);
        g_pArial18BitmapFont->SetTextColor(D3DCOLOR_XRGB(240, 240, 0));
        g_pArial18BitmapFont->TextOut(pFrontBuffer, L"Y", -1, 110, 240);
        g_pArial18BitmapFont->SetTextColor(D3DCOLOR_XRGB(240, 240, 240));
        g_pArial18BitmapFont->TextOut(pFrontBuffer, L"Carefully check which one is correct before doing anything!", -1, 128, 240);

        g_pD3DDevice->Present(NULL, NULL, NULL, NULL);

        pFrontBuffer->Release();
    }
}

void App::DrawLastLoadedTable(const int table_id) {
    wchar_t trailer[100];
    switch (table_id)
    {
        case 0:
            swprintf(trailer, L"Current table [Kernel]", g_lba48_version);
            break;
        case 1:
            swprintf(trailer, L"Current table [HDD]", g_lba48_version);
            break;
        case 2:
            swprintf(trailer, L"Current table [Backup]", g_lba48_version);
            break;
        default:
            break;
    }
    DisplayText(trailer, 400, 20, COLOR_WHITE, XFONT_LEFT);
}

void App::DoFormatAll()
{
    int i;

    DisplayWarning();
    XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);
    XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);

    if (aButton == 3)
    {
        XboxPartitionTable table = tableManipulator->GetPartitionTable();
        if (write_partition_tables(table))
        {
            g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
            DrawScreen(g_iCurrentPart);
            DisplayText(L"Partition table written...", 60, 428, COLOR_GREEN);
            g_pD3DDevice->Present(NULL, NULL, NULL, NULL);
            Sleep(500);

            int ypos = 14;

            // Format modified partitions
            for (i = 0; i < XboxPartitionTableEntryCount; i++)
            {
                ULONGLONG new_partition_start = tableManipulator->GetPartitionStart(i);
                ULONGLONG new_partition_size = tableManipulator->GetPartitionSize(i);

                ULONGLONG existing_partition_start = tableManipulatorOriginal->GetPartitionStart(i);
                ULONGLONG existing_partition_size = tableManipulatorOriginal->GetPartitionSize(i);

                // Check if partition actually changed
                if (tableManipulator->isPartitionEnabled(i) &&
                    (existing_partition_size != new_partition_size || existing_partition_start != new_partition_start))
                {
                    int cluster_size = tableManipulator->CalculateClusterSizeForPartition(i);
                    g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
                    DrawScreen(g_iCurrentPart);
                    wchar_t format_str[50];
                    swprintf(format_str, L"Formatting partition %d with %dk clusters...", i + 1, cluster_size);
                    DisplayText(format_str, 60, 428, COLOR_GREEN);
                    g_pD3DDevice->Present(NULL, NULL, NULL, NULL);
                    format_partition(i);
                }
            }

            // Update the original table since we saved a new copy
            tableManipulatorOriginal->LoadNewTable(table);
        }
    }
}

void App::DoWriteTable()
{
    g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

    DrawScreen(g_iCurrentPart);
    DisplayText(L"Partition table will be written to hard drive!", 60, 428, COLOR_WHITE);
    DisplayText(L"Press", 60, 448, COLOR_WHITE);
    DisplayText(L"Y", 110, 448, COLOR_YELLOW);
    DisplayText(L"to continue or any other button to abort.", 128, 448, COLOR_WHITE);

    g_pD3DDevice->Present(NULL, NULL, NULL, NULL);

    XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);
    XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);

    if (aButton == 3)
    {

        XboxPartitionTable table = tableManipulator->GetPartitionTable();
        if (write_partition_tables(table))
        {
            g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
            DrawScreen(g_iCurrentPart);
            DisplayText(L"Partition table written...", 60, 428, COLOR_GREEN);
            DisplayText(L"Press any button to continue.", 60, 448, COLOR_WHITE);
            g_pD3DDevice->Present(NULL, NULL, NULL, NULL);
            XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);
        }
        else
        {
            g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
            DrawScreen(g_iCurrentPart);
            DisplayText(L"Partition table write failed!", 60, 428, COLOR_RED);
            DisplayText(L"Press any button to continue.", 60, 448, COLOR_WHITE);
            g_pD3DDevice->Present(NULL, NULL, NULL, NULL);
            XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);
            XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);
        }
    }
}

void App::DoFormatOne()
{
    int cluster_size = tableManipulator->CalculateClusterSizeForPartition(g_iCurrentPart);
    DisplayWarning();
    XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);
    XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);

    if (aButton == 3)
    {
        // Update kernel partition table if changed, else we might format a partition with wrong start/size
        if (CheckIfPartitionTableChanged() && KernelPartTblAdd)
        {
            XboxPartitionTable table = tableManipulator->GetPartitionTable();
            memcpy(KernelPartTblAdd, &table, sizeof(XboxPartitionTable));
        }

        g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
        DrawScreen(g_iCurrentPart);
        wchar_t format_str[50];
        swprintf(format_str, L"formatting partition %d with %dk clusters...", g_iCurrentPart + 1, cluster_size);
        DisplayText(format_str, 60, 428, COLOR_GREEN);
        g_pD3DDevice->Present(NULL, NULL, NULL, NULL);

        format_partition(g_iCurrentPart);
    }
}

void App::DoStart()
{
    switch (g_iFunction)
    {
    case FUNCTION_WRITE_TABLE:
        DoWriteTable();
        break;
    case FUNCTION_WRITE_TABLE_FORMAT:
        DoFormatAll();
        break;
    case FUNCTION_FORMAT_CURRENT_PARTITION:
        if (tableManipulator->isPartitionEnabled(g_iCurrentPart))
        {
            DoFormatOne();
        }
        break;
    default:
        break;
    }
}

void App::part_change_size(int TrigValue, int change_start, int Direction, bool fine)
{
    int lba_delta;

    if (fine)
    {
        // Aligned to 4k boundaries
        lba_delta = 8;
    }
    else if (TrigValue < 0x60)
    {
        lba_delta = 64;
        Sleep(250);
    }
    else if (TrigValue > 0xf0)
    {
        lba_delta = SECTORS_PER_CLUSTER * 0x20000;
        Sleep(250);
    }
    else
    {
        TrigValue >>= 4;
        lba_delta = SECTORS_PER_CLUSTER * 2 << (TrigValue - 4);
    }

    int ops = change_start * 2 + (Direction < 0);
    switch (ops)
    {
    case 0:
        tableManipulator->PartitionIncreaseSizeBy(g_iCurrentPart, lba_delta);
        break;
    case 1:
        tableManipulator->PartitionDecreaseSizeBy(g_iCurrentPart, lba_delta);
        break;
    case 2:
        tableManipulator->PartitionMoveForward(g_iCurrentPart, lba_delta);
        break;
    case 3:
        tableManipulator->PartitionMoveBack(g_iCurrentPart, lba_delta);
        break;
    default:
        break;
    }
}

App::App() {
    tableManipulator = NULL;
    tableManipulatorOriginal = NULL;

    g_start_size_selector = 0;
    g_iFunction = FUNCTION_WRITE_TABLE;

    SCREEN_SIZE_X = 640;
    SCREEN_SIZE_Y = 480;

    g_pD3D = NULL;
    g_pD3DDevice = NULL;
    g_pBackTexture = NULL;
    g_iFunction = FUNCTION_WRITE_TABLE;

    // no static asserts in vc++ from this vintage
    if (XboxPartitionTableSize != HDD_SECTOR_SIZE || XboxPartitionTableSize != sizeof(XboxPartitionTable))
    {
        return;
    }

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        RtlInitAnsiString(&partition_str[i], partStr[i]);
    }

    LD_LAUNCH_DASHBOARD LaunchDashData = {XLD_LAUNCH_DASHBOARD_MAIN_MENU};

    InitialiseD3D();

    XDEVICE_PREALLOC_TYPE deviceTypes[] =
        {
            {XDEVICE_TYPE_GAMEPAD, 4},
        };

    XInitDevices(sizeof(deviceTypes) / sizeof(XDEVICE_PREALLOC_TYPE), deviceTypes);

    Sleep(2000);

    g_Controller[0] = XInputOpen(XDEVICE_TYPE_GAMEPAD, XDEVICE_PORT0, XDEVICE_NO_SLOT, 0);
    g_Controller[1] = XInputOpen(XDEVICE_TYPE_GAMEPAD, XDEVICE_PORT1, XDEVICE_NO_SLOT, 0);
    g_Controller[2] = XInputOpen(XDEVICE_TYPE_GAMEPAD, XDEVICE_PORT2, XDEVICE_NO_SLOT, 0);
    g_Controller[3] = XInputOpen(XDEVICE_TYPE_GAMEPAD, XDEVICE_PORT3, XDEVICE_NO_SLOT, 0);

    g_TotalHDDSectors = get_hdd_size();

    // Read kernel patch info + kernel reported partition table
    if (!reload_lba48())
    {
        DisplayNoInfo();
    }

    // Read partition table from HDD
    tableHDD = read_hdd_main_partition_table();
    tableHDDBackup = read_hdd_backup_partition_table();

    if (!XboxPartitionTableManipulator::HasMagic(tableHDD) && XboxPartitionTableManipulator::HasMagic(tableHDDBackup))
    {
        DisplayWarningBackupAvailable();
    }

    // save a copy so we can check if partitions changed
    tableManipulatorOriginal->LoadNewTable(tableKernel);

    g_iCurrentPart = tableManipulator->GetLastAvailablePartitionSlot();

    for (int i = 0x0ff; i > 0x06; i -= 0x06)
    {
        ULONG rect_color = i << 24;

        g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
        DrawScreen(g_iCurrentPart);
        DrawInfo();
        DrawFreeSpace();
        g_pD3DDevice->Present(NULL, NULL, NULL, NULL);
    }
}

App::~App() {
    if (tableManipulatorOriginal)
    {
        delete (tableManipulatorOriginal);
    }
    if (tableManipulator)
    {
        delete (tableManipulator);
    }

    CleanUpD3D();
}

void App::MainLoop() {
    int iCurrentScheme = 0;
    int iCurrentHddScheme = 0;
    int iWaitRelease = 0;

    CheckTableStatus();
    
    while (true)
    {
        g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
        DrawScreen(g_iCurrentPart);
        DrawInfo();
        DrawFreeSpace();
        DrawFormatCluster();
        DrawLastLoadedTable(iCurrentHddScheme);

        g_pD3DDevice->Present(NULL, NULL, NULL, NULL);

        if (iWaitRelease)
        {
            XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);
        }

        XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY, 0);

        iWaitRelease = 0;

        if (!tButton && !aLThumbY)
        {
            switch (aButton)
            {
            case XINPUT_GAMEPAD_A:
                // Cycle standard partition schemes
                switch (iCurrentScheme)
                {
                case 0:
                    tableManipulator->SetupPartitionsStock();
                    iCurrentScheme++;
                    break;
                case 1:
                    tableManipulator->SetupPartitionsFOnly();
                    iCurrentScheme++;
                    break;
                case 2:
                    tableManipulator->SetupPartitionsFGSplit();
                    iCurrentScheme = 0;
                    break;
                default:
                    iCurrentScheme = 0;
                }
                iWaitRelease = 1;
                break;

            case XINPUT_GAMEPAD_B:
                // No-op
                iWaitRelease = 1;
                break;

            case XINPUT_GAMEPAD_X:
                // Toggle change lba size/start
                g_start_size_selector = !g_start_size_selector;
                iWaitRelease = 1;
                break;

            case XINPUT_GAMEPAD_Y:
                // Cycle through available partition tables
                if (iCurrentHddScheme == 2)
                {
                    iCurrentHddScheme = 0;
                }
                else
                {
                    iCurrentHddScheme++;
                }

                switch (iCurrentHddScheme)
                {
                case 0:
                    tableManipulator->LoadNewTable(tableManipulatorOriginal->GetPartitionTable());
                    break;

                case 1:
                    tableManipulator->LoadNewTable(tableHDD);
                    break;

                case 2:
                    tableManipulator->LoadNewTable(tableHDDBackup);
                    break;

                default:
                    break;
                }
                iWaitRelease = 1;
                break;

            case XINPUT_GAMEPAD_BLACK:
                g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
                DrawScreen(g_iCurrentPart);
                DisplayText(L"Partition table will be erased!", 60, 428, COLOR_WHITE);
                DisplayText(L"Press", 60, 448, COLOR_WHITE);
                DisplayText(L"Y", 110, 448, COLOR_YELLOW);
                DisplayText(L"to continue or any other key to abort!", 128, 448, COLOR_WHITE);
                g_pD3DDevice->Present(NULL, NULL, NULL, NULL);

                XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);
                XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);
                if (aButton == 3)
                {
                    if (erase_partition_tables())
                    {
                        g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
                        DrawScreen(g_iCurrentPart);
                        DisplayText(L"Partition table erased.", 60, 428, COLOR_GREEN);
                        DisplayText(L"Press any button to continue.", 60, 448, COLOR_WHITE);
                        g_pD3DDevice->Present(NULL, NULL, NULL, NULL);
                        XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);
                    }
                    else
                    {
                        g_pD3DDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
                        DrawScreen(g_iCurrentPart);
                        DisplayText(L"Partition table erasure failed!", 60, 428, COLOR_RED);
                        DisplayText(L"Press any button to continue.", 60, 448, COLOR_WHITE);
                        g_pD3DDevice->Present(NULL, NULL, NULL, NULL);
                        XInputGetStateAny(&aButton, &aButtonVal, &tButton, &aLThumbY);
                    }
                }
                break;

            case XINPUT_GAMEPAD_WHITE:
                // Flip between current functions
                if (g_iFunction < 2)
                {
                    g_iFunction++;
                }
                else
                {
                    g_iFunction = FUNCTION_WRITE_TABLE;
                }
                iWaitRelease = 1;
                break;

            case XINPUT_GAMEPAD_LEFT_TRIGGER: // smaller
                if (tableManipulator->isPartitionEnabled(g_iCurrentPart))
                {
                    part_change_size(aButtonVal, g_start_size_selector, -1, false);
                }
                break;

            case XINPUT_GAMEPAD_RIGHT_TRIGGER: // bigger
                if (!tableManipulator->isPartitionEnabled(g_iCurrentPart))
                {
                    tableManipulator->EnablePartition(g_iCurrentPart);
                }
                else
                {
                    part_change_size(aButtonVal, g_start_size_selector, 1, false);
                }
                break;

            default:
                break;
            }
        }
        else
        {
            if (aLThumbY > 28671)
            {
                part_change_size(aButtonVal, g_start_size_selector, 1, true);
                continue;
            }
            if (aLThumbY < -28672)
            {
                part_change_size(aButtonVal, g_start_size_selector, -1, true);
                continue;
            }
            if (tButton & XINPUT_GAMEPAD_LEFT_THUMB && tButton & XINPUT_GAMEPAD_RIGHT_THUMB)
            {
                tableManipulator->LoadNewTable(tableManipulatorOriginal->GetPartitionTable());
                g_iCurrentPart = tableManipulator->GetLastAvailablePartitionSlot();
                iWaitRelease = 1;
                continue;
            }
            else if (tButton & XINPUT_GAMEPAD_RIGHT_THUMB)
            {
                if (tableManipulator->isPartitionEnabled(g_iCurrentPart))
                {
                    tableManipulator->DisablePartition(g_iCurrentPart);
                    if (g_iCurrentPart)
                    {
                        g_iCurrentPart--;
                    }
                }
                else
                {
                    tableManipulator->EnablePartition(g_iCurrentPart);
                }

                iWaitRelease = 1;
            }
            else if (tButton & XINPUT_GAMEPAD_DPAD_RIGHT && tableManipulator->isPartitionEnabled(g_iCurrentPart))
            {
                if (g_start_size_selector)
                {
                    tableManipulator->PartitionMoveForward(g_iCurrentPart, 0x200000);
                }
                else
                {
                    tableManipulator->PartitionIncreaseSizeBy(g_iCurrentPart, 0x200000);
                }
                Sleep(250);
            }
            else if (tButton & XINPUT_GAMEPAD_DPAD_LEFT && tableManipulator->isPartitionEnabled(g_iCurrentPart))
            {
                if (g_start_size_selector)
                {
                    tableManipulator->PartitionMoveBack(g_iCurrentPart, 0x200000);
                }
                else
                {
                    tableManipulator->PartitionDecreaseSizeBy(g_iCurrentPart, 0x200000);
                }
                Sleep(250);
            }
            else if (tButton & XINPUT_GAMEPAD_DPAD_UP && g_iCurrentPart > 0)
            {
                if (!tableManipulator->GetPartitionSize(g_iCurrentPart))
                {
                    tableManipulator->DisablePartition(g_iCurrentPart);
                }
                g_iCurrentPart--;
                Sleep(150);
            }
            else if (tButton & XINPUT_GAMEPAD_DPAD_DOWN && g_iCurrentPart < (XboxPartitionTableEntryCount - 1))
            {
                if (!tableManipulator->GetPartitionSize(g_iCurrentPart))
                {
                    tableManipulator->DisablePartition(g_iCurrentPart);
                }
                g_iCurrentPart++;
                Sleep(150);
            }
            else if (tButton & XINPUT_GAMEPAD_BACK)
            {
                DisplayHelp();
            }
            else if (tButton & XINPUT_GAMEPAD_START)
            {
                DoStart();
            }
        }
    }
}

//-----------------------------------------------------------------------------
// Name: main()
// Desc: The application's entry point
//-----------------------------------------------------------------------------
void __cdecl main()
{
    App app;
    app.MainLoop();
}
