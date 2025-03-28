/** @file
  Configuration Manager Data of Heterogeneous Memory Attribute Table (HMAT)

  Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PlatformResourceLib.h>
#include <Library/FloorSweepingLib.h>

#include <ConfigurationManagerObject.h>
#include <Protocol/ConfigurationManagerDataProtocol.h>
#include <ConfigurationManagerDataPrivate.h>

#include <TH500/TH500Definitions.h>

#define NORMALIZED_UNREACHABLE_LATENCY    0xFFFF
#define NORMALIZED_UNREACHABLE_BANDWIDTH  0x0

#define ENTRY_BASE_UNIT_NANO_SEC_TO_PICO_SEC  0x3E8
#define ENTRY_BASE_UNIT_GBPS_TO_MBPS          0x3E8

EFI_STATUS
EFIAPI
InstallHeterogeneousMemoryAttributeTable (
  IN OUT  EDKII_PLATFORM_REPOSITORY_INFO  **PlatformRepositoryInfo,
  IN      UINTN                           PlatformRepositoryInfoEnd,
  IN      EDKII_PLATFORM_REPOSITORY_INFO  *NVIDIAPlatformRepositoryInfo
  )
{
  EDKII_PLATFORM_REPOSITORY_INFO          *Repo;
  CM_STD_OBJ_ACPI_TABLE_INFO              *NewAcpiTables;
  CM_ARM_LOCALITY_LATENCY_BANDWIDTH_INFO  *SysInfo;
  UINT32                                  SysInfoStructCount;
  UINT32                                  SysInfoStructIndex;
  UINT16                                  *ReadLatencyList;
  UINT16                                  *WriteLatencyList;
  UINT16                                  *AccessBandwidthList;
  UINT32                                  NumInitiatorProximityDomains;
  UINT32                                  NumTargetProximityDomains;
  UINT32                                  *InitiatorProximityDomainList;
  UINT32                                  *TargetProximityDomainList;
  UINT32                                  Index;
  UINT32                                  MaxEnabledHbmDmns;

  // Create a ACPI Table Entry
  for (Index = 0; Index < PcdGet32 (PcdConfigMgrObjMax); Index++) {
    if (NVIDIAPlatformRepositoryInfo[Index].CmObjectId == CREATE_CM_STD_OBJECT_ID (EStdObjAcpiTableList)) {
      NewAcpiTables = (CM_STD_OBJ_ACPI_TABLE_INFO *)AllocateCopyPool (
                                                      NVIDIAPlatformRepositoryInfo[Index].CmObjectSize +
                                                      (sizeof (CM_STD_OBJ_ACPI_TABLE_INFO)),
                                                      NVIDIAPlatformRepositoryInfo[Index].CmObjectPtr
                                                      );

      if (NewAcpiTables == NULL) {
        return EFI_OUT_OF_RESOURCES;
      }

      NVIDIAPlatformRepositoryInfo[Index].CmObjectPtr = NewAcpiTables;

      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].AcpiTableSignature = EFI_ACPI_6_4_HETEROGENEOUS_MEMORY_ATTRIBUTE_TABLE_SIGNATURE;
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].AcpiTableRevision  = EFI_ACPI_6_4_HETEROGENEOUS_MEMORY_ATTRIBUTE_TABLE_REVISION;
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].TableGeneratorId   = CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdHmat);
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].AcpiTableData      = NULL;
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].OemTableId         = 0;
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].OemRevision        = FixedPcdGet64 (PcdAcpiDefaultOemRevision);
      NVIDIAPlatformRepositoryInfo[Index].CmObjectCount++;
      NVIDIAPlatformRepositoryInfo[Index].CmObjectSize += sizeof (CM_STD_OBJ_ACPI_TABLE_INFO);

      break;
    } else if (NVIDIAPlatformRepositoryInfo[Index].CmObjectPtr == NULL) {
      break;
    }
  }

  Repo = *PlatformRepositoryInfo;

  SysInfoStructCount = 6; // Maximum types allowed by this structure
  SysInfoStructIndex = 0;

  SysInfo = (CM_ARM_LOCALITY_LATENCY_BANDWIDTH_INFO *)AllocateZeroPool (sizeof (CM_ARM_LOCALITY_LATENCY_BANDWIDTH_INFO) * SysInfoStructCount);
  if (SysInfo == NULL) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to allocate System Latency and Bandwidth Info structures\r\n", __FUNCTION__));
    return EFI_OUT_OF_RESOURCES;
  }

  // Proximity Domains
  MaxEnabledHbmDmns            = GetMaxHbmPxmDomains ();
  NumInitiatorProximityDomains = (TH500_GPU_HBM_PXM_DOMAIN_START > MaxEnabledHbmDmns) ? TH500_GPU_HBM_PXM_DOMAIN_START : MaxEnabledHbmDmns;
  NumTargetProximityDomains    = (TH500_GPU_HBM_PXM_DOMAIN_START > MaxEnabledHbmDmns) ? TH500_GPU_HBM_PXM_DOMAIN_START : MaxEnabledHbmDmns;

  // Generate and populate Initiator proximity domain list
  InitiatorProximityDomainList = (UINT32 *)AllocateZeroPool (sizeof (UINT32) * NumInitiatorProximityDomains);
  if (InitiatorProximityDomainList == NULL) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to allocate Initiator Proximity DomainList entry\r\n", __FUNCTION__));
    return EFI_OUT_OF_RESOURCES;
  }

  for (Index = 0; Index < NumInitiatorProximityDomains; Index++) {
    InitiatorProximityDomainList[Index] = Index;
  }

  // Generate and populate Target proximity domain list
  TargetProximityDomainList = (UINT32 *)AllocateZeroPool (sizeof (UINT32) * NumTargetProximityDomains);
  if (TargetProximityDomainList == NULL) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to allocate Target Proximity DomainList entry\r\n", __FUNCTION__));
    return EFI_OUT_OF_RESOURCES;
  }

  for (Index = 0; Index < NumTargetProximityDomains; Index++) {
    TargetProximityDomainList[Index] = Index;
  }

  ReadLatencyList = (UINT16 *)AllocateZeroPool (sizeof (UINT16) * NumInitiatorProximityDomains * NumTargetProximityDomains);
  if (ReadLatencyList == NULL) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to allocate Read Latency list entry\r\n", __FUNCTION__));
    return EFI_OUT_OF_RESOURCES;
  }

  WriteLatencyList = (UINT16 *)AllocateZeroPool (sizeof (UINT16) * NumInitiatorProximityDomains * NumTargetProximityDomains);
  if (WriteLatencyList == NULL) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to allocate Write Latency list entry\r\n", __FUNCTION__));
    return EFI_OUT_OF_RESOURCES;
  }

  AccessBandwidthList = (UINT16 *)AllocateZeroPool (sizeof (UINT16) * NumInitiatorProximityDomains * NumTargetProximityDomains);
  if (AccessBandwidthList == NULL) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to allocate Access Bandwidth list entry\r\n", __FUNCTION__));
    return EFI_OUT_OF_RESOURCES;
  }

  // Populate with max latency or least bandwidth
  for (UINTN InitIndex = 0; InitIndex < NumInitiatorProximityDomains; InitIndex++) {
    for (UINTN TargIndex = 0; TargIndex < NumTargetProximityDomains; TargIndex++) {
      ReadLatencyList[InitIndex * NumInitiatorProximityDomains + TargIndex]     = NORMALIZED_UNREACHABLE_LATENCY;
      WriteLatencyList[InitIndex * NumInitiatorProximityDomains + TargIndex]    = NORMALIZED_UNREACHABLE_LATENCY;
      AccessBandwidthList[InitIndex * NumInitiatorProximityDomains + TargIndex] = NORMALIZED_UNREACHABLE_BANDWIDTH;
    }
  }

  // cpu to local and remote cpus
  for (UINTN InitIndex = 0; InitIndex < PcdGet32 (PcdTegraMaxSockets); InitIndex++) {
    // check if socket enabled for this Index
    if (!IsSocketEnabled (InitIndex)) {
      continue;
    }

    for (UINTN TargIndex = 0; TargIndex <  PcdGet32 (PcdTegraMaxSockets); TargIndex++) {
      if (!IsSocketEnabled (TargIndex)) {
        continue;
      }

      if (InitIndex == TargIndex) {
        // cpu to local cpu
        ReadLatencyList[InitIndex * NumInitiatorProximityDomains + TargIndex]     = PcdGet32 (PcdCpuToLocalCpuReadLatency);
        WriteLatencyList[InitIndex * NumInitiatorProximityDomains + TargIndex]    = PcdGet32 (PcdCpuToLocalCpuWriteLatency);
        AccessBandwidthList[InitIndex * NumInitiatorProximityDomains + TargIndex] = PcdGet32 (PcdCpuToLocalCpuAccessBandwidth);
      } else {
        // cpu to remote cpu
        ReadLatencyList[InitIndex * NumInitiatorProximityDomains + TargIndex]     = PcdGet32 (PcdCpuToRemoteCpuReadLatency);
        WriteLatencyList[InitIndex * NumInitiatorProximityDomains + TargIndex]    = PcdGet32 (PcdCpuToRemoteCpuWriteLatency);
        AccessBandwidthList[InitIndex * NumInitiatorProximityDomains + TargIndex] = PcdGet32 (PcdCpuToRemoteCpuAccessBandwidth);
      }
    }
  }

  // cpu to local gpu hbm and remote gpu hbm
  for (UINTN InitIndex = 0; InitIndex < PcdGet32 (PcdTegraMaxSockets); InitIndex++) {
    // check if socket enabled for this Index
    if (!IsSocketEnabled (InitIndex)) {
      continue;
    }

    for (UINTN TargIndex = TH500_GPU_HBM_PXM_DOMAIN_START; TargIndex < NumTargetProximityDomains; TargIndex++) {
      if (!IsSocketEnabled ((TargIndex - TH500_GPU_HBM_PXM_DOMAIN_START)/TH500_GPU_MAX_NR_MEM_PARTITIONS)) {
        continue;
      }

      if ((TargIndex >= TH500_GPU_HBM_PXM_DOMAIN_START_FOR_GPU_ID (InitIndex)) &&
          (TargIndex < (TH500_GPU_HBM_PXM_DOMAIN_START_FOR_GPU_ID (InitIndex) + TH500_GPU_MAX_NR_MEM_PARTITIONS)))
      {
        // local hbm
        ReadLatencyList[InitIndex * NumInitiatorProximityDomains + TargIndex]     = PcdGet32 (PcdCpuToLocalHbmReadLatency);
        WriteLatencyList[InitIndex * NumInitiatorProximityDomains + TargIndex]    = PcdGet32 (PcdCpuToLocalHbmWriteLatency);
        AccessBandwidthList[InitIndex * NumInitiatorProximityDomains + TargIndex] = PcdGet32 (PcdCpuToLocalHbmAccessBandwidth);
      } else {
        // remote hbm
        ReadLatencyList[InitIndex * NumInitiatorProximityDomains + TargIndex]     = PcdGet32 (PcdCpuToRemoteHbmReadLatency);
        WriteLatencyList[InitIndex * NumInitiatorProximityDomains + TargIndex]    = PcdGet32 (PcdCpuToRemoteHbmWriteLatency);
        AccessBandwidthList[InitIndex * NumInitiatorProximityDomains + TargIndex] = PcdGet32 (PcdCpuToRemoteHbmAccessBandwidth);
      }
    }
  }

  // gpu to local hbm and remote hbm
  for (UINTN InitIndex = TH500_GPU_PXM_DOMAIN_START; InitIndex < TH500_GPU_PXM_DOMAIN_START + PcdGet32 (PcdTegraMaxSockets); InitIndex++) {
    // check if CPU socket enabled for this GPU Index
    if (!IsSocketEnabled (InitIndex - TH500_GPU_PXM_DOMAIN_START)) {
      continue;
    }

    // for all proximity domains
    for (UINTN TargIndex = TH500_GPU_HBM_PXM_DOMAIN_START;
         TargIndex < NumTargetProximityDomains;
         TargIndex++)
    {
      if (!IsSocketEnabled ((TargIndex - TH500_GPU_HBM_PXM_DOMAIN_START)/TH500_GPU_MAX_NR_MEM_PARTITIONS)) {
        continue;
      }

      if ((TargIndex >= TH500_GPU_HBM_PXM_DOMAIN_START_FOR_GPU_ID (InitIndex-TH500_GPU_PXM_DOMAIN_START)) &&
          (TargIndex < TH500_GPU_HBM_PXM_DOMAIN_START_FOR_GPU_ID (InitIndex-TH500_GPU_PXM_DOMAIN_START) + TH500_GPU_MAX_NR_MEM_PARTITIONS))
      {
        // local hbm
        ReadLatencyList[InitIndex * NumInitiatorProximityDomains + TargIndex]     = PcdGet32 (PcdGpuToLocalHbmReadLatency);
        WriteLatencyList[InitIndex * NumInitiatorProximityDomains + TargIndex]    = PcdGet32 (PcdGpuToLocalHbmWriteLatency);
        AccessBandwidthList[InitIndex * NumInitiatorProximityDomains + TargIndex] = PcdGet32 (PcdGpuToLocalHbmAccessBandwidth);
      } else {
        // remote hbm
        ReadLatencyList[InitIndex * NumInitiatorProximityDomains + TargIndex]     = PcdGet32 (PcdGpuToRemoteHbmReadLatency);
        WriteLatencyList[InitIndex * NumInitiatorProximityDomains + TargIndex]    = PcdGet32 (PcdGpuToRemoteHbmWriteLatency);
        AccessBandwidthList[InitIndex * NumInitiatorProximityDomains + TargIndex] = PcdGet32 (PcdGpuToRemoteHbmAccessBandwidth);
      }
    }
  }

  // gpu to local cpu and remote cpu
  for (UINTN InitIndex = TH500_GPU_PXM_DOMAIN_START; InitIndex < TH500_GPU_PXM_DOMAIN_START + PcdGet32 (PcdTegraMaxSockets); InitIndex++) {
    // check if CPU socket enabled for this GPU Index
    if (!IsSocketEnabled (InitIndex - TH500_GPU_PXM_DOMAIN_START)) {
      continue;
    }

    for (UINTN TargIndex = 0; TargIndex <  PcdGet32 (PcdTegraMaxSockets); TargIndex++) {
      if (!IsSocketEnabled (TargIndex)) {
        continue;
      }

      if ((InitIndex - TH500_GPU_PXM_DOMAIN_START) == TargIndex) {
        // local cpu
        ReadLatencyList[InitIndex * NumInitiatorProximityDomains + TargIndex]     = PcdGet32 (PcdGpuToLocalCpuReadLatency);
        WriteLatencyList[InitIndex * NumInitiatorProximityDomains + TargIndex]    = PcdGet32 (PcdGpuToLocalCpuWriteLatency);
        AccessBandwidthList[InitIndex * NumInitiatorProximityDomains + TargIndex] = PcdGet32 (PcdGpuToLocalCpuAccessBandwidth);
      } else {
        // remote cpu
        ReadLatencyList[InitIndex * NumInitiatorProximityDomains + TargIndex]     = PcdGet32 (PcdGpuToRemoteCpuReadLatency);
        WriteLatencyList[InitIndex * NumInitiatorProximityDomains + TargIndex]    = PcdGet32 (PcdGpuToRemoteCpuWriteLatency);
        AccessBandwidthList[InitIndex * NumInitiatorProximityDomains + TargIndex] = PcdGet32 (PcdGpuToRemoteCpuAccessBandwidth);
      }
    }
  }

  // Populate Read Latency SysInfo Structure
  SysInfo[SysInfoStructIndex].Flags.MemoryHierarchy = 0x0;
  SysInfo[SysInfoStructIndex].DataType              = EFI_ACPI_HMAT_READ_LATENCY_DATATYPE;
  // TODO: create macros for other options
  SysInfo[SysInfoStructIndex].MinTransferSize              = 1;
  SysInfo[SysInfoStructIndex].EntryBaseUnit                = ENTRY_BASE_UNIT_NANO_SEC_TO_PICO_SEC;
  SysInfo[SysInfoStructIndex].NumInitiatorProximityDomains = NumInitiatorProximityDomains;
  SysInfo[SysInfoStructIndex].NumTargetProximityDomains    = NumTargetProximityDomains;
  SysInfo[SysInfoStructIndex].InitiatorProximityDomainList = InitiatorProximityDomainList;
  SysInfo[SysInfoStructIndex].TargetProximityDomainList    = TargetProximityDomainList;
  SysInfo[SysInfoStructIndex].LatencyBandwidthEntries      = ReadLatencyList;
  SysInfoStructIndex++;

  // Populate Write Latency SysInfo Structure
  SysInfo[SysInfoStructIndex].Flags.MemoryHierarchy = 0x0;
  SysInfo[SysInfoStructIndex].DataType              = EFI_ACPI_HMAT_WRITE_LATENCY_DATATYPE;
  // TODO: create macros for other options
  SysInfo[SysInfoStructIndex].MinTransferSize              = 1;
  SysInfo[SysInfoStructIndex].EntryBaseUnit                = ENTRY_BASE_UNIT_NANO_SEC_TO_PICO_SEC;
  SysInfo[SysInfoStructIndex].NumInitiatorProximityDomains = NumInitiatorProximityDomains;
  SysInfo[SysInfoStructIndex].NumTargetProximityDomains    = NumTargetProximityDomains;
  SysInfo[SysInfoStructIndex].InitiatorProximityDomainList = InitiatorProximityDomainList;
  SysInfo[SysInfoStructIndex].TargetProximityDomainList    = TargetProximityDomainList;
  SysInfo[SysInfoStructIndex].LatencyBandwidthEntries      = WriteLatencyList;
  SysInfoStructIndex++;

  // Populate Access Bandwidth SysInfo Structure
  SysInfo[SysInfoStructIndex].Flags.MemoryHierarchy = 0x0;
  SysInfo[SysInfoStructIndex].DataType              = EFI_ACPI_HMAT_ACCESS_BANDWIDTH_DATATYPE;
  // TODO: create macros for other options
  SysInfo[SysInfoStructIndex].MinTransferSize              = 1;
  SysInfo[SysInfoStructIndex].EntryBaseUnit                = ENTRY_BASE_UNIT_GBPS_TO_MBPS;
  SysInfo[SysInfoStructIndex].NumInitiatorProximityDomains = NumInitiatorProximityDomains;
  SysInfo[SysInfoStructIndex].NumTargetProximityDomains    = NumTargetProximityDomains;
  SysInfo[SysInfoStructIndex].InitiatorProximityDomainList = InitiatorProximityDomainList;
  SysInfo[SysInfoStructIndex].TargetProximityDomainList    = TargetProximityDomainList;
  SysInfo[SysInfoStructIndex].LatencyBandwidthEntries      = AccessBandwidthList;
  SysInfoStructIndex++;

  Repo->CmObjectId    = CREATE_CM_ARM_OBJECT_ID (EArmObjLocalityLatencyBandwidthInfo);
  Repo->CmObjectToken = CM_NULL_TOKEN;
  Repo->CmObjectSize  = sizeof (CM_ARM_LOCALITY_LATENCY_BANDWIDTH_INFO) * SysInfoStructIndex;
  Repo->CmObjectCount = SysInfoStructIndex;
  Repo->CmObjectPtr   = SysInfo;
  Repo++;

  ASSERT ((UINTN)Repo <= PlatformRepositoryInfoEnd);

  *PlatformRepositoryInfo = Repo;

  return EFI_SUCCESS;
}
