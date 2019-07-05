
set(DPDK_LIBRARY_PATH "$ENV{RTE_SDK}/$ENV{RTE_TARGET}/lib")

find_library (dpdk_PMD_I40E_LIBRARY rte_pmd_i40e PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_PMD_IXGBE_LIBRARY rte_pmd_ixgbe PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_PMD_E1000_LIBRARY rte_pmd_e1000 PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_PMD_BNXT_LIBRARY rte_pmd_bnxt PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_PMD_RING_LIBRARY rte_pmd_ring PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_PMD_CXGBE_LIBRARY rte_pmd_cxgbe PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_PMD_ENA_LIBRARY rte_pmd_ena PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_PMD_ENIC_LIBRARY rte_pmd_enic PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_PMD_FM10K_LIBRARY rte_pmd_fm10k PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_PMD_NFP_LIBRARY rte_pmd_nfp PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_PMD_QEDE_LIBRARY rte_pmd_qede PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_RING_LIBRARY rte_ring PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_KVARGS_LIBRARY rte_kvargs PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_MEMPOOL_LIBRARY rte_mempool PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_MEMPOOL_RING_LIBRARY rte_mempool_ring PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_PMD_SFC_EFX_LIBRARY rte_pmd_sfc_efx PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_HASH_LIBRARY rte_hash PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_CMDLINE_LIBRARY rte_cmdline PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_MBUF_LIBRARY rte_mbuf PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_CFGFILE_LIBRARY rte_cfgfile PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_EAL_LIBRARY rte_eal PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_ETHDEV_LIBRARY rte_ethdev PATH ${DPDK_LIBRARY_PATH})
find_library (dpdk_KVARGS_LIBRARY rte_kvargs PATH ${DPDK_LIBRARY_PATH})

find_library (dpdk_LIBRARY 
    rte_pmd_i40e
    rte_pmd_ixgbe
  PATH
    ${DPDK_LIBRARY_PATH}
)


include (FindPackageHandleStandardArgs)
find_package_handle_standard_args(dpdk
  REQUIRED_VAR
    dpdk_PMD_I40E_LIBRARY
    dpdk_PMD_IXGBE_LIBRARY
    dpdk_PMD_E1000_LIBRARY
    dpdk_PMD_BNXT_LIBRARY
    dpdk_PMD_RING_LIBRARY
    dpdk_PMD_CXGBE_LIBRARY
    dpdk_PMD_ENA_LIBRARY
    dpdk_PMD_ENIC_LIBRARY
    dpdk_PMD_FM10K_LIBRARY
    dpdk_PMD_NFP_LIBRARY
    dpdk_PMD_QEDE_LIBRARY
    dpdk_RING_LIBRARY
    dpdk_KVARGS_LIBRARY
    dpdk_MEMPOOL_LIBRARY
    dpdk_MEMPOOL_RING_LIBRARY
    dpdk_PMD_SFC_EFX_LIBRARY
    dpdk_HASH_LIBRARY
    dpdk_CMDLINE_LIBRARY
    dpdk_MBUF_LIBRARY
    dpdk_CFGFILE_LIBRARY
    dpdk_EAL_LIBRARY
    dpdk_ETHDEV_LIBRARY
    dpdk_KVARGS_LIBRARY)

if (dpdk_FOUND)
  set (dpdk_LIBRARIES
    ${dpdk_PMD_I40E_LIBRARY}
    ${dpdk_PMD_IXGBE_LIBRARY}
    ${dpdk_PMD_E1000_LIBRARY}
    ${dpdk_PMD_BNXT_LIBRARY}
    ${dpdk_PMD_RING_LIBRARY}
    ${dpdk_PMD_CXGBE_LIBRARY}
    ${dpdk_PMD_ENA_LIBRARY}
    ${dpdk_PMD_ENIC_LIBRARY}
    ${dpdk_PMD_FM10K_LIBRARY}
    ${dpdk_PMD_NFP_LIBRARY}
    ${dpdk_PMD_QEDE_LIBRARY}
    ${dpdk_RING_LIBRARY}
    ${dpdk_KVARGS_LIBRARY}
    ${dpdk_MEMPOOL_LIBRARY}
    ${dpdk_MEMPOOL_RING_LIBRARY}
    ${dpdk_PMD_SFC_EFX_LIBRARY}
    ${dpdk_HASH_LIBRARY}
    ${dpdk_CMDLINE_LIBRARY}
    ${dpdk_MBUF_LIBRARY}
    ${dpdk_CFGFILE_LIBRARY}
    ${dpdk_EAL_LIBRARY}
    ${dpdk_ETHDEV_LIBRARY}
    ${dpdk_KVARGS_LIBRARY})
  
  add_library(dpdk INTERFACE IMPORTED)

  set_target_properties(dpdk
    PROPERTIES 
      INTERFACE_LINK_LIBRARIES "${dpdk_LIBRARIES}")
endif()



