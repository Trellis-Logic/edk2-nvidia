/** @file
*
*  Copyright (c) 2018-2023, NVIDIA CORPORATION. All rights reserved.
*
*  SPDX-License-Identifier: BSD-2-Clause-Patent
*
**/

#include "TH500ResourceConfig.h"

#include <Uefi.h>
#include <Pi/PiMultiPhase.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/ErotLib.h>
#include <Library/HobLib.h>
#include <Library/IoLib.h>
#include <Library/DramCarveoutLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TegraPlatformInfoLib.h>
#include <Library/GoldenRegisterLib.h>
#include "TH500ResourceConfigPrivate.h"
#include <TH500/TH500Definitions.h>

TEGRA_MMIO_INFO  TH500MmioInfo[] = {
  {
    TH500_GIC_ITS_BASE,
    SIZE_64KB
  },
  {
    TH500_WDT_CTRL_BASE,
    SIZE_4KB
  },
  {
    TH500_WDT_RFRSH_BASE,
    SIZE_4KB
  },
  // Placeholder for memory in DRAM CO CARVEOUT_CCPLEX_INTERWORLD_SHMEM that would
  // be treated as MMIO memory.
  {
    0,
    0
  },
  {
    0,
    0
  }
};

TEGRA_MMIO_INFO  TH500GicRedistributorMmioInfo[] = {
  {
    TH500_GIC_REDISTRIBUTOR_BASE_SOCKET_0,
    SIZE_256KB *TH500_GIC_REDISTRIBUTOR_INSTANCES
  },
  {
    TH500_GIC_REDISTRIBUTOR_BASE_SOCKET_1,
    SIZE_256KB *TH500_GIC_REDISTRIBUTOR_INSTANCES
  },
  {
    TH500_GIC_REDISTRIBUTOR_BASE_SOCKET_2,
    SIZE_256KB *TH500_GIC_REDISTRIBUTOR_INSTANCES
  },
  {
    TH500_GIC_REDISTRIBUTOR_BASE_SOCKET_3,
    SIZE_256KB *TH500_GIC_REDISTRIBUTOR_INSTANCES
  },
};

TEGRA_MMIO_INFO  TH500SocketScratchMmioInfo[] = {
  {
    TH500_SCRATCH_BASE_SOCKET_0,
    TH500_SCRATCH_SIZE
  },
  {
    TH500_SCRATCH_BASE_SOCKET_1,
    TH500_SCRATCH_SIZE
  },
  {
    TH500_SCRATCH_BASE_SOCKET_2,
    TH500_SCRATCH_SIZE
  },
  {
    TH500_SCRATCH_BASE_SOCKET_3,
    TH500_SCRATCH_SIZE
  },
};

TEGRA_MMIO_INFO  TH500SocketCbbMmioInfo[] = {
  {
    TH500_CBB_FABRIC_BASE_SOCKET_0,
    TH500_CBB_FABRIC_SIZE
  },
  {
    TH500_CBB_FABRIC_BASE_SOCKET_1,
    TH500_CBB_FABRIC_SIZE
  },
  {
    TH500_CBB_FABRIC_BASE_SOCKET_2,
    TH500_CBB_FABRIC_SIZE
  },
  {
    TH500_CBB_FABRIC_BASE_SOCKET_3,
    TH500_CBB_FABRIC_SIZE
  },
};

TEGRA_MMIO_INFO  TH500SocketMssMmioInfo[] = {
  {
    TH500_MSS_BASE_SOCKET_0,
    TH500_MSS_SIZE
  },
  {
    TH500_MSS_BASE_SOCKET_1,
    TH500_MSS_SIZE
  },
  {
    TH500_MSS_BASE_SOCKET_2,
    TH500_MSS_SIZE
  },
  {
    TH500_MSS_BASE_SOCKET_3,
    TH500_MSS_SIZE
  },
};

TEGRA_BASE_AND_SIZE_INFO  TH500EgmMemoryInfo[TH500_MAX_SOCKETS] = { };

/**
  Get Socket Mask

**/
UINT32
EFIAPI
TH500GetSocketMask (
  IN UINTN  CpuBootloaderAddress
  )
{
  TEGRA_CPUBL_PARAMS  *CpuBootloaderParams;
  UINT32              SocketMask;

  CpuBootloaderParams = (TEGRA_CPUBL_PARAMS *)(VOID *)CpuBootloaderAddress;

  SocketMask = CpuBootloaderParams->SocketMask;
  ASSERT (SocketMask != 0);
  ASSERT (HighBitSet32 (SocketMask) < TH500_MAX_SOCKETS);

  return SocketMask;
}

/**
  Retrieve UART Instance Info

  This function retrieves the base address of that UART instance, and sets the known UART type
  based on the UART instance number.

  @param[in]  UARTInstanceNumber    UART instance number
  @param[out] UARTInstanceType      UART instance type
  @param[out] UARTInstanceAddress   UART instance address

  @retval TRUE    UART instance info was successfullly retrieved
  @retval FALSE   Retrieval of UART instance info failed

**/
BOOLEAN
TH500UARTInstanceInfo (
  IN  UINT32                UARTInstanceNumber,
  OUT UINT32                *UARTInstanceType,
  OUT EFI_PHYSICAL_ADDRESS  *UARTInstanceAddress
  )
{
  EFI_PHYSICAL_ADDRESS  UARTBaseAddress[] = {
    0x0,
    TEGRA_UART_ADDRESS_0,
    TEGRA_UART_ADDRESS_1,
  };

  *UARTInstanceAddress = 0;
  *UARTInstanceType    = TEGRA_UART_TYPE_NONE;

  if ((UARTInstanceNumber >= ARRAY_SIZE (UARTBaseAddress)) ||
      ((BIT (UARTInstanceNumber) & TEGRA_UART_SUPPORT_FLAG) == 0x0))
  {
    return FALSE;
  }

  *UARTInstanceType    = TEGRA_UART_TYPE_SBSA;
  *UARTInstanceAddress = UARTBaseAddress[UARTInstanceNumber];

  return TRUE;
}

/**
  Installs resources into the HOB list

  This function install all memory regions into the HOB list.
  This function is called by the platform memory initialization library.

  @param  NumberOfMemoryRegions Number of regions installed into HOB list.

  @retval EFI_SUCCESS           Resources have been installed
  @retval EFI_DEVICE_ERROR      Error setting up memory

**/
EFI_STATUS
TH500GetResourceConfig (
  IN UINTN                 CpuBootloaderAddress,
  OUT TEGRA_RESOURCE_INFO  *PlatformInfo
  )
{
  TEGRA_CPUBL_PARAMS  *CpuBootloaderParams;
  NVDA_MEMORY_REGION  *DramRegions;
  NVDA_MEMORY_REGION  *CarveoutRegions;
  UINTN               CarveoutRegionsCount = 0;
  UINTN               Index;
  UINTN               Socket;
  UINT32              SocketMask;
  TH500_MEMORY_MODE   MemoryMode;

  Index       = 0;
  DramRegions = NULL;

  CpuBootloaderParams = (TEGRA_CPUBL_PARAMS *)(VOID *)CpuBootloaderAddress;

  PlatformInfo->DtbLoadAddress = TH500GetDTBBaseAddress ((UINTN)CpuBootloaderParams);

  SocketMask = TH500GetSocketMask (CpuBootloaderAddress);

  DEBUG ((EFI_D_ERROR, "SocketMask=0x%x\n", SocketMask));

  // Detect the memory mode
  MemoryMode = Th500MemoryModeNormal;
  if ((CpuBootloaderParams->CarveoutInfo[TH500_PRIMARY_SOCKET][CARVEOUT_EGM].Base != 0) &&
      (CpuBootloaderParams->CarveoutInfo[TH500_PRIMARY_SOCKET][CARVEOUT_EGM].Size != 0))
  {
    if ((CpuBootloaderParams->CarveoutInfo[TH500_PRIMARY_SOCKET][CARVEOUT_HV].Base != 0) &&
        (CpuBootloaderParams->CarveoutInfo[TH500_PRIMARY_SOCKET][CARVEOUT_HV].Size != 0))
    {
      MemoryMode = Th500MemoryModeEgmWithHv;
    } else {
      MemoryMode = Th500MemoryModeEgmNoHv;
    }
  }

  // Build dram regions
  if (MemoryMode == Th500MemoryModeNormal) {
    DEBUG ((EFI_D_ERROR, "Memory Mode: Normal\n"));
    DramRegions = (NVDA_MEMORY_REGION *)AllocatePool (sizeof (NVDA_MEMORY_REGION) * TH500_MAX_SOCKETS);
    ASSERT (DramRegions != NULL);
    if (DramRegions == NULL) {
      return EFI_DEVICE_ERROR;
    }

    Index = 0;
    for (Socket = TH500_PRIMARY_SOCKET; Socket < TH500_MAX_SOCKETS; Socket++) {
      if (!(SocketMask & (1UL << Socket))) {
        continue;
      }

      DramRegions[Index].MemoryBaseAddress = CpuBootloaderParams->SdramInfo[Socket].Base;
      DramRegions[Index].MemoryLength      = CpuBootloaderParams->SdramInfo[Socket].Size;
      Index++;
    }
  } else if (MemoryMode == Th500MemoryModeEgmNoHv) {
    DEBUG ((EFI_D_ERROR, "Memory Mode: EGM No HV\n"));
    // When egm is enabled without hv, uefi should use only memory in egm carveout on all sockets and rcm, os and uefi carveouts
    // only on primary socket.
    DramRegions = (NVDA_MEMORY_REGION *)AllocatePool ((sizeof (NVDA_MEMORY_REGION) * TH500_MAX_SOCKETS) + (sizeof (NVDA_MEMORY_REGION) * 3));
    ASSERT (DramRegions != NULL);
    if (DramRegions == NULL) {
      return EFI_DEVICE_ERROR;
    }

    Index = 0;
    for (Socket = TH500_PRIMARY_SOCKET; Socket < TH500_MAX_SOCKETS; Socket++) {
      if (!(SocketMask & (1UL << Socket))) {
        continue;
      }

      DramRegions[Index].MemoryBaseAddress = CpuBootloaderParams->CarveoutInfo[Socket][CARVEOUT_EGM].Base;
      DramRegions[Index].MemoryLength      = CpuBootloaderParams->CarveoutInfo[Socket][CARVEOUT_EGM].Size;
      Index++;

      if (Socket == TH500_PRIMARY_SOCKET) {
        if ((CpuBootloaderParams->CarveoutInfo[Socket][CARVEOUT_RCM_BLOB].Base != 0) &&
            (CpuBootloaderParams->CarveoutInfo[Socket][CARVEOUT_RCM_BLOB].Size != 0))
        {
          DramRegions[Index].MemoryBaseAddress = CpuBootloaderParams->CarveoutInfo[Socket][CARVEOUT_RCM_BLOB].Base;
          DramRegions[Index].MemoryLength      = CpuBootloaderParams->CarveoutInfo[Socket][CARVEOUT_RCM_BLOB].Size;
          Index++;
        }

        if ((CpuBootloaderParams->CarveoutInfo[Socket][CARVEOUT_OS].Base != 0) &&
            (CpuBootloaderParams->CarveoutInfo[Socket][CARVEOUT_OS].Size != 0))
        {
          DramRegions[Index].MemoryBaseAddress = CpuBootloaderParams->CarveoutInfo[Socket][CARVEOUT_OS].Base;
          DramRegions[Index].MemoryLength      = CpuBootloaderParams->CarveoutInfo[Socket][CARVEOUT_OS].Size;
          Index++;
        }

        DramRegions[Index].MemoryBaseAddress = CpuBootloaderParams->CarveoutInfo[Socket][CARVEOUT_UEFI].Base;
        DramRegions[Index].MemoryLength      = CpuBootloaderParams->CarveoutInfo[Socket][CARVEOUT_UEFI].Size;
        Index++;
      }
    }
  } else if (MemoryMode == Th500MemoryModeEgmWithHv) {
    DEBUG ((EFI_D_ERROR, "Memory Mode: EGM With HV\n"));
    DramRegions = (NVDA_MEMORY_REGION *)AllocatePool (sizeof (NVDA_MEMORY_REGION) * TH500_MAX_SOCKETS);
    ASSERT (DramRegions != NULL);
    if (DramRegions == NULL) {
      return EFI_DEVICE_ERROR;
    }

    Index = 0;
    for (Socket = TH500_PRIMARY_SOCKET; Socket < TH500_MAX_SOCKETS; Socket++) {
      if (!(SocketMask & (1UL << Socket))) {
        continue;
      }

      DramRegions[Index].MemoryBaseAddress = CpuBootloaderParams->SdramInfo[Socket].Base;
      DramRegions[Index].MemoryLength      = CpuBootloaderParams->SdramInfo[Socket].Size;
      Index++;
    }
  }

  PlatformInfo->DramRegions         = DramRegions;
  PlatformInfo->DramRegionsCount    = Index;
  PlatformInfo->UefiDramRegionIndex = 0;

  // Build Carveout regions
  CarveoutRegions = (NVDA_MEMORY_REGION *)AllocatePool (sizeof (NVDA_MEMORY_REGION) * TH500_MAX_SOCKETS * CARVEOUT_OEM_COUNT);
  ASSERT (CarveoutRegions != NULL);
  if (CarveoutRegions == NULL) {
    return EFI_DEVICE_ERROR;
  }

  for (Socket = TH500_PRIMARY_SOCKET; Socket < TH500_MAX_SOCKETS; Socket++) {
    if (!(SocketMask & (1 << Socket))) {
      continue;
    }

    for (Index = CARVEOUT_NONE; Index < CARVEOUT_OEM_COUNT; Index++) {
      if ((CpuBootloaderParams->CarveoutInfo[Socket][Index].Base == 0) ||
          (CpuBootloaderParams->CarveoutInfo[Socket][Index].Size == 0))
      {
        continue;
      }

      DEBUG ((
        EFI_D_ERROR,
        "Socket: %d Carveout %d Region: Base: 0x%016lx, Size: 0x%016lx\n",
        Socket,
        Index,
        CpuBootloaderParams->CarveoutInfo[Socket][Index].Base,
        CpuBootloaderParams->CarveoutInfo[Socket][Index].Size
        ));
      if (Index == CARVEOUT_CCPLEX_INTERWORLD_SHMEM) {
        if (Socket == TH500_PRIMARY_SOCKET) {
          // For primary socket, add memory in DRAM CO CARVEOUT_CCPLEX_INTERWORLD_SHMEM in its placeholder
          // in TH500MmioInfo for MMIO mapping.
          TH500MmioInfo[ARRAY_SIZE (TH500MmioInfo)-2].Base = CpuBootloaderParams->CarveoutInfo[Socket][Index].Base;
          TH500MmioInfo[ARRAY_SIZE (TH500MmioInfo)-2].Size = CpuBootloaderParams->CarveoutInfo[Socket][Index].Size;
        }

        CarveoutRegions[CarveoutRegionsCount].MemoryBaseAddress = CpuBootloaderParams->CarveoutInfo[Socket][Index].Base;
        CarveoutRegions[CarveoutRegionsCount].MemoryLength      = CpuBootloaderParams->CarveoutInfo[Socket][Index].Size;
        CarveoutRegionsCount++;
      } else if ((Index == CARVEOUT_RCM_BLOB) ||
                 (Index == CARVEOUT_UEFI) ||
                 (Index == CARVEOUT_OS))
      {
        // Leave in memory map but marked as used on socket 0
        if (Socket == TH500_PRIMARY_SOCKET) {
          BuildMemoryAllocationHob (
            CpuBootloaderParams->CarveoutInfo[Socket][Index].Base,
            EFI_PAGES_TO_SIZE (EFI_SIZE_TO_PAGES (CpuBootloaderParams->CarveoutInfo[Socket][Index].Size)),
            EfiReservedMemoryType
            );
        }
      } else if (Index == CARVEOUT_EGM) {
        if (MemoryMode == Th500MemoryModeEgmNoHv) {
          continue;
        }

        CarveoutRegions[CarveoutRegionsCount].MemoryBaseAddress = CpuBootloaderParams->CarveoutInfo[Socket][Index].Base;
        CarveoutRegions[CarveoutRegionsCount].MemoryLength      = CpuBootloaderParams->CarveoutInfo[Socket][Index].Size;
        CarveoutRegionsCount++;
      } else if (Index == CARVEOUT_HV) {
        continue;
      } else {
        CarveoutRegions[CarveoutRegionsCount].MemoryBaseAddress = CpuBootloaderParams->CarveoutInfo[Socket][Index].Base;
        CarveoutRegions[CarveoutRegionsCount].MemoryLength      = CpuBootloaderParams->CarveoutInfo[Socket][Index].Size;
        CarveoutRegionsCount++;
      }
    }
  }

  PlatformInfo->CarveoutRegions      = CarveoutRegions;
  PlatformInfo->CarveoutRegionsCount = CarveoutRegionsCount;

  if (CpuBootloaderParams->EarlyBootVariables->Data.Mb1Data.UefiDebugLevel == 0) {
    CpuBootloaderParams->EarlyBootVariables->Data.Mb1Data.UefiDebugLevel = PcdGet32 (PcdDebugPrintErrorLevel);
  }

  return EFI_SUCCESS;
}

/**
  Retrieve DTB Address

**/
UINT64
TH500GetDTBBaseAddress (
  IN UINTN  CpuBootloaderAddress
  )
{
  TEGRA_CPUBL_PARAMS          *CpuBootloaderParams;
  UINT64                      MemoryBase;
  UINT64                      MemorySize;
  EFI_FIRMWARE_VOLUME_HEADER  *FvHeader;
  UINT64                      FvOffset;
  UINT64                      FvSize;

  CpuBootloaderParams = (TEGRA_CPUBL_PARAMS *)(VOID *)CpuBootloaderAddress;
  MemoryBase          = CpuBootloaderParams->CarveoutInfo[TH500_PRIMARY_SOCKET][CARVEOUT_UEFI].Base;
  MemorySize          = CpuBootloaderParams->CarveoutInfo[TH500_PRIMARY_SOCKET][CARVEOUT_UEFI].Size;
  FvHeader            = NULL;
  FvOffset            = 0;

  while (FvOffset < MemorySize) {
    FvHeader = (EFI_FIRMWARE_VOLUME_HEADER *)(VOID *)(MemoryBase + FvOffset);
    if (FvHeader->Signature == EFI_FVH_SIGNATURE) {
      break;
    }

    FvOffset += SIZE_64KB;
  }

  ASSERT (FvOffset < MemorySize);
  ASSERT (FvHeader != NULL);
  FvSize = FvHeader->FvLength;
  // Make UEFI FV size aligned to 64KB.
  FvSize = ALIGN_VALUE (FvSize, SIZE_64KB);

  return (UINT64)FvHeader + FvSize;
}

/**
  Retrieve MMIO Base and Size

**/
TEGRA_MMIO_INFO *
EFIAPI
TH500GetMmioBaseAndSize (
  IN UINT32  SocketMask
  )
{
  TEGRA_MMIO_INFO  *MmioInfo;
  TEGRA_MMIO_INFO  *MmioInfoEnd;
  UINTN            Socket;

  MmioInfo = AllocateZeroPool (
               sizeof (TH500MmioInfo) +
               (TH500_MAX_SOCKETS * 4 * sizeof (TEGRA_MMIO_INFO))
               );
  CopyMem (MmioInfo, TH500MmioInfo, sizeof (TH500MmioInfo));

  // point to the table terminating entry copied from TH500MmioInfo
  MmioInfoEnd = MmioInfo + (sizeof (TH500MmioInfo) / sizeof (TEGRA_MMIO_INFO)) - 1;

  for (Socket = 0; Socket < TH500_MAX_SOCKETS; Socket++) {
    if (!(SocketMask & (1UL << Socket))) {
      continue;
    }

    CopyMem (MmioInfoEnd++, &TH500GicRedistributorMmioInfo[Socket], sizeof (TEGRA_MMIO_INFO));
    CopyMem (MmioInfoEnd++, &TH500SocketScratchMmioInfo[Socket], sizeof (TEGRA_MMIO_INFO));
    CopyMem (MmioInfoEnd++, &TH500SocketCbbMmioInfo[Socket], sizeof (TEGRA_MMIO_INFO));
    CopyMem (MmioInfoEnd++, &TH500SocketMssMmioInfo[Socket], sizeof (TEGRA_MMIO_INFO));
  }

  return MmioInfo;
}

/**
  Retrieve Active Boot Chain

**/
EFI_STATUS
EFIAPI
TH500GetActiveBootChain (
  IN  UINTN   CpuBootloaderAddress,
  IN  UINTN   Socket,
  OUT UINT32  *BootChain
  )
{
  UINT64  ScratchAddr;

  ScratchAddr = TH500SocketScratchMmioInfo[Socket].Base + TH500_BOOT_CHAIN_SCRATCH_OFFSET;

  *BootChain = MmioBitFieldRead32 (
                 ScratchAddr,
                 BOOT_CHAIN_BIT_FIELD_LO,
                 BOOT_CHAIN_BIT_FIELD_HI
                 );

  if (*BootChain >= BOOT_CHAIN_MAX) {
    return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}

/**
  Validate Active Boot Chain

**/
EFI_STATUS
EFIAPI
TH500ValidateActiveBootChain (
  IN  UINTN  CpuBootloaderAddress
  )
{
  UINT32      SocketMask;
  UINTN       Socket;
  UINT64      ScratchAddr;
  EFI_STATUS  Status;
  UINT32      BootChain;

  DEBUG ((DEBUG_INFO, "%a: Entry\n", __FUNCTION__));

  SocketMask = TH500GetSocketMask (CpuBootloaderAddress);
  for (Socket = 0; Socket < TH500_MAX_SOCKETS; Socket++) {
    if (!(SocketMask & (1UL << Socket))) {
      continue;
    }

    Status = TH500GetActiveBootChain (CpuBootloaderAddress, Socket, &BootChain);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: GetActiveBootChain failed socket %u: %r\n", __FUNCTION__, Socket, Status));
      continue;
    }

    ScratchAddr = TH500SocketScratchMmioInfo[Socket].Base + TH500_BOOT_CHAIN_SCRATCH_OFFSET;

    MmioBitFieldWrite32 (
      ScratchAddr,
      BOOT_CHAIN_STATUS_LO + BootChain,
      BOOT_CHAIN_STATUS_LO + BootChain,
      BOOT_CHAIN_GOOD
      );

    Status = ErotSendBootComplete (Socket, BootChain);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: ErotSendBootComplete failed socket %u: %r\n", __FUNCTION__, Socket, Status));
    } else {
      DEBUG ((DEBUG_ERROR, "BootComplete successful, socket %u\n", Socket));
    }
  }

  return EFI_SUCCESS;
}

/**
  Get Platform Resource Information

**/
EFI_STATUS
EFIAPI
TH500GetPlatformResourceInformation (
  IN UINTN                         CpuBootloaderAddress,
  IN TEGRA_PLATFORM_RESOURCE_INFO  *PlatformResourceInfo,
  IN BOOLEAN                       InMm
  )
{
  EFI_STATUS          Status;
  TEGRA_CPUBL_PARAMS  *CpuBootloaderParams;
  UINT32              SocketMask;
  UINTN               Index;

  CpuBootloaderParams = (TEGRA_CPUBL_PARAMS *)(VOID *)CpuBootloaderAddress;

  SocketMask = TH500GetSocketMask (CpuBootloaderAddress);

  PlatformResourceInfo->SocketMask = SocketMask;

  /* Avoid this step when called from MM */
  if (InMm == FALSE) {
    Status = TH500GetActiveBootChain (CpuBootloaderAddress, 0, &PlatformResourceInfo->ActiveBootChain);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Status = TH500GetResourceConfig (CpuBootloaderAddress, PlatformResourceInfo->ResourceInfo);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    PlatformResourceInfo->MmioInfo = TH500GetMmioBaseAndSize (SocketMask);
  }

  PlatformResourceInfo->RamdiskOSInfo.Base = CpuBootloaderParams->CarveoutInfo[TH500_PRIMARY_SOCKET][CARVEOUT_OS].Base;
  PlatformResourceInfo->RamdiskOSInfo.Size = CpuBootloaderParams->CarveoutInfo[TH500_PRIMARY_SOCKET][CARVEOUT_OS].Size;

  PlatformResourceInfo->RcmBlobInfo.Base = CpuBootloaderParams->CarveoutInfo[TH500_PRIMARY_SOCKET][CARVEOUT_RCM_BLOB].Base;
  PlatformResourceInfo->RcmBlobInfo.Size = CpuBootloaderParams->CarveoutInfo[TH500_PRIMARY_SOCKET][CARVEOUT_RCM_BLOB].Size;

  if ((PlatformResourceInfo->RcmBlobInfo.Base != 0) &&
      (PlatformResourceInfo->RcmBlobInfo.Size != 0))
  {
    PlatformResourceInfo->BootType = TegrablBootRcm;
  } else {
    PlatformResourceInfo->BootType = TegrablBootColdBoot;
  }

  if ((CpuBootloaderParams->CarveoutInfo[TH500_PRIMARY_SOCKET][CARVEOUT_EGM].Base != 0) &&
      (CpuBootloaderParams->CarveoutInfo[TH500_PRIMARY_SOCKET][CARVEOUT_EGM].Size != 0))
  {
    if ((CpuBootloaderParams->CarveoutInfo[TH500_PRIMARY_SOCKET][CARVEOUT_HV].Base != 0) &&
        (CpuBootloaderParams->CarveoutInfo[TH500_PRIMARY_SOCKET][CARVEOUT_HV].Size != 0))
    {
      PlatformResourceInfo->HypervisorMode = TRUE;
    }
  }

  PlatformResourceInfo->EgmMemoryInfo = TH500EgmMemoryInfo;
  for (Index = 0; Index < TH500_MAX_SOCKETS; Index++) {
    PlatformResourceInfo->EgmMemoryInfo[Index].Base = CpuBootloaderParams->CarveoutInfo[Index][CARVEOUT_EGM].Base;
    PlatformResourceInfo->EgmMemoryInfo[Index].Size = CpuBootloaderParams->CarveoutInfo[Index][CARVEOUT_EGM].Size;
  }

  // Populate Total Memory.
  for (Index = 0, PlatformResourceInfo->PhysicalDramSize = 0; Index < TH500_MAX_SOCKETS; Index++) {
    if (!(SocketMask & (1UL << Index))) {
      continue;
    }

    PlatformResourceInfo->PhysicalDramSize += CpuBootloaderParams->SdramInfo[Index].Size;
  }

  BuildGuidDataHob (&gNVIDIATH500MB1DataGuid, &CpuBootloaderParams->EarlyBootVariables, sizeof (CpuBootloaderParams->EarlyBootVariables));

  return EFI_SUCCESS;
}

/**
 * Get Partition information.
**/
EFI_STATUS
EFIAPI
TH500GetPartitionInfo (
  IN  UINTN   CpuBootloaderAddress,
  IN  UINT32  PartitionIndex,
  OUT UINT16  *DeviceInstance,
  OUT UINT64  *PartitionStartByte,
  OUT UINT64  *PartitionSizeBytes
  )
{
  TEGRA_CPUBL_PARAMS      *CpuBootloaderParams;
  TEGRABL_PARTITION_DESC  *PartitionDesc;

  CpuBootloaderParams = (TEGRA_CPUBL_PARAMS *)(VOID *)CpuBootloaderAddress;

  if (PartitionIndex >= TEGRABL_BINARY_MAX) {
    DEBUG ((
      DEBUG_ERROR,
      "%a, Partition Index is invalid %u (Max %u)\n",
      __FUNCTION__,
      PartitionIndex,
      TEGRABL_BINARY_MAX
      ));
    return EFI_INVALID_PARAMETER;
  }

  PartitionDesc       = &CpuBootloaderParams->PartitionInfo[PartitionIndex][PRIMARY_COPY];
  *DeviceInstance     = PartitionDesc->DeviceInstance;
  *PartitionStartByte = PartitionDesc->StartBlock * BLOCK_SIZE;
  *PartitionSizeBytes = PartitionDesc->Size;

  return EFI_SUCCESS;
}
