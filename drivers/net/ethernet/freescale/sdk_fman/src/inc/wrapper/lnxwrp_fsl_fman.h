/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
 * Copyright 2019 NXP
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/******************************************************************************
 @File		lnxwrp_fsl_fman.h

 @Description	Linux internal kernel API
*//***************************************************************************/

#ifndef __LNXWRP_FSL_FMAN_H
#define __LNXWRP_FSL_FMAN_H

#include <linux/types.h>
#include <linux/device.h>   /* struct device */
#include <linux/fsl_qman.h> /* struct qman_fq */
#include "dpaa_integration_ext.h"
#include "fm_port_ext.h"
#include "fm_mac_ext.h"
#include "fm_rtc_ext.h"

/**************************************************************************//**
 @Group		FM_LnxKern_grp Frame Manager Linux wrapper API

 @Description	FM API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group		FM_LnxKern_ctrl_grp Control Unit

 @Description	Control Unit

		Internal Kernel Control Unit API
 @{
*//***************************************************************************/

/*****************************************************************************/
/*                  Internal Linux kernel routines                           */
/*****************************************************************************/

typedef t_Handle handle_t;

/**************************************************************************//**
 @Description	FM device opaque structure used for type checking
*//***************************************************************************/
struct fm;

/**************************************************************************//**
 @Description	FM MAC device opaque structure used for type checking
*//***************************************************************************/
struct fm_mac_dev;

/**************************************************************************//**
 @Description	A structure ..,
*//***************************************************************************/
struct fm_port;

typedef int (*alloc_pcd_fqids)(struct device *dev, uint32_t num,
			       uint8_t alignment, uint32_t *base_fqid);

typedef int (*free_pcd_fqids)(struct device *dev, uint32_t base_fqid);

struct fm_port_pcd_param {
	alloc_pcd_fqids	 cba;
	free_pcd_fqids	 cbf;
	struct device	*dev;
};

/**************************************************************************//**
 @Description	A structure of information about each of the external
		buffer pools used by the port,
*//***************************************************************************/
struct fm_port_pool_param {
	uint8_t		id;		/**< External buffer pool id */
	uint16_t	size;		/**< External buffer pool buffer size */
};

/**************************************************************************//**
 @Description   structure for additional port parameters
*//***************************************************************************/
struct fm_port_params {
	uint32_t errq;	    /**< Error Queue Id. */
	uint32_t defq;	    /**< For Tx and HC - Default Confirmation queue,
				 0 means no Tx conf for processed frames.
				 For Rx and OP - default Rx queue. */
	uint8_t	num_pools;  /**< Number of pools use by this port */
	struct fm_port_pool_param pool_param[FM_PORT_MAX_NUM_OF_EXT_POOLS];
			    /**< Parameters for each pool */
	uint16_t priv_data_size;  /**< Area that user may save for his own
				       need (E.g. save the SKB) */
	bool parse_results; /**< Put the parser-results in the Rx/Tx buffer */
	bool hash_results;  /**< Put the hash-results in the Rx/Tx buffer */
	bool time_stamp;    /**< Put the time-stamp in the Rx/Tx buffer */
	bool frag_enable;   /**< Fragmentation support, for OP only */
	uint16_t data_align;  /**< value for selecting a data alignment (must be a power of 2);
                               if write optimization is used, must be >= 16. */
	uint8_t manip_extra_space;  /**< Maximum extra size needed (insertion-size minus removal-size);
                                     Note that this field impacts the size of the buffer-prefix
                                     (i.e. it pushes the data offset); */
};

/**************************************************************************//**
 @Function	fm_bind

 @Description	Bind to a specific FM device.

 @Param[in]	fm_dev	- the OF handle of the FM device.

 @Return	A handle of the FM device.

 @Cautions	Allowed only after the port was created.
*//***************************************************************************/
struct fm *fm_bind(struct device *fm_dev);

/**************************************************************************//**
 @Function	fm_unbind

 @Description	Un-bind from a specific FM device.

 @Param[in]	fm	- A handle of the FM device.

 @Cautions	Allowed only after the port was created.
*//***************************************************************************/
void fm_unbind(struct fm *fm);

void *fm_get_handle(struct fm *fm);
void *fm_get_rtc_handle(struct fm *fm);
struct resource *fm_get_mem_region(struct fm *fm);

/**************************************************************************//**
 @Function	fm_port_bind

 @Description	Bind to a specific FM-port device (may be Rx or Tx port).

 @Param[in]	fm_port_dev - the OF handle of the FM port device.

 @Return	A handle of the FM port device.

 @Cautions	Allowed only after the port was created.
*//***************************************************************************/
struct fm_port *fm_port_bind(struct device *fm_port_dev);

/**************************************************************************//**
 @Function	fm_port_unbind

 @Description	Un-bind from a specific FM-port device (may be Rx or Tx port).

 @Param[in]	port	- A handle of the FM port device.

 @Cautions	Allowed only after the port was created.
*//***************************************************************************/
void fm_port_unbind(struct fm_port *port);

/**************************************************************************//**
 @Function	fm_set_rx_port_params

 @Description	Configure parameters for a specific Rx FM-port device.

 @Param[in]	port	- A handle of the FM port device.
 @Param[in]	params	- Rx port parameters

 @Cautions	Allowed only after the port is binded.
*//***************************************************************************/
void fm_set_rx_port_params(struct fm_port *port,
			   struct fm_port_params *params);

/**************************************************************************//**
 @Function	fm_port_pcd_bind

 @Description	Bind as a listener on a port PCD.

 @Param[in]	port	- A handle of the FM port device.
 @Param[in]	params	- PCD port parameters

 @Cautions	Allowed only after the port is binded.
*//***************************************************************************/
void fm_port_pcd_bind (struct fm_port *port, struct fm_port_pcd_param *params);

/**************************************************************************//**
 @Function	fm_port_get_buff_layout_ext_params

 @Description	Get data_align and manip_extra_space from the device tree
                chosen node if applied.
                This function will only update these two parameters.
                When this port has no such parameters in the device tree
                values will be set to 0.

 @Param[in]	port	- A handle of the FM port device.
 @Param[in]	params	- PCD port parameters

 @Cautions	Allowed only after the port is binded.
*//***************************************************************************/
void fm_port_get_buff_layout_ext_params(struct fm_port *port, struct fm_port_params *params);

/**************************************************************************//**
 @Function	fm_get_tx_port_channel

 @Description	Get qman-channel number for this Tx port.

 @Param[in]	port	- A handle of the FM port device.

 @Return	qman-channel number for this Tx port.

 @Cautions	Allowed only after the port is binded.
*//***************************************************************************/
uint16_t fm_get_tx_port_channel(struct fm_port *port);

/**************************************************************************//**
 @Function	fm_set_tx_port_params

 @Description	Configure parameters for a specific Tx FM-port device

 @Param[in]	port	- A handle of the FM port device.
 @Param[in]	params	- Tx port parameters

 @Cautions	Allowed only after the port is binded.
*//***************************************************************************/
void fm_set_tx_port_params(struct fm_port *port, struct fm_port_params *params);


/**************************************************************************//**
 @Function	fm_mac_set_handle

 @Description	Set mac handle

 @Param[in]	h_lnx_wrp_fm_dev - A handle of the LnxWrp FM device.
 @Param[in]	h_fm_mac	 - A handle of the LnxWrp FM MAC device.
 @Param[in]	mac_id		 - MAC id.
*//***************************************************************************/
void fm_mac_set_handle(t_Handle h_lnx_wrp_fm_dev, t_Handle h_fm_mac,
		       int mac_id);

/**************************************************************************//**
 @Function	fm_port_enable

 @Description	Enable specific FM-port device (may be Rx or Tx port).

 @Param[in]	port	- A handle of the FM port device.

 @Cautions	Allowed only after the port is initialized.
*//***************************************************************************/
int fm_port_enable(struct fm_port *port);

/**************************************************************************//**
 @Function	fm_port_disable

 @Description	Disable specific FM-port device (may be Rx or Tx port).

 @Param[in]	port	- A handle of the FM port device.

 @Cautions	Allowed only after the port is initialized.
*//***************************************************************************/
int fm_port_disable(struct fm_port *port);

void *fm_port_get_handle(const struct fm_port *port);

u64 *fm_port_get_buffer_time_stamp(const struct fm_port *port,
		const void *data);

/**************************************************************************//**
 @Function	fm_mutex_lock

 @Description   Lock function required before any FMD/LLD call.
*//***************************************************************************/
void fm_mutex_lock(void);

/**************************************************************************//**
 @Function	fm_mutex_unlock

 @Description   Unlock function required after any FMD/LLD call.
*//***************************************************************************/
void fm_mutex_unlock(void);

/**************************************************************************//**
 @Function	fm_get_max_frm

 @Description   Get the maximum frame size
*//***************************************************************************/
int fm_get_max_frm(void);

/**************************************************************************//**
 @Function	fm_get_rx_extra_headroom

 @Description   Get the extra headroom size
*//***************************************************************************/
int fm_get_rx_extra_headroom(void);

/**************************************************************************//**
 @Function	fm_has_errata_a050385

 @Description   Detect if the SoC is vulnerable to the A050385 errata
*//***************************************************************************/
#ifdef FM_ERRATUM_A050385
bool fm_has_errata_a050385(void);
#endif

/**************************************************************************//**
@Function     fm_port_enable_rx_l4csum

@Description   Configure L4 checksum validation by setting the DCL4C bit in
               the FMBM_RFNE[FDCS] register. Passing true sets the DCL4C bit
               to 1, disabling L4 checksum validation. This setting is
               relevant only if the parser is enabled (Rx port).

@Param[in]    port - A handle of the FM port device.
@Param[in]    enable - Boolean, set to true to enable L4 checksum validation
                       and false to disable it.

@Cautions     Allowed only after the port is initialized.
*//***************************************************************************/
int fm_port_enable_rx_l4csum(struct fm_port *port, bool enable);

struct   auto_res_tables_sizes
{
	uint16_t   max_num_of_arp_entries;
	uint16_t   max_num_of_echo_ipv4_entries;
	uint16_t   max_num_of_ndp_entries;
	uint16_t   max_num_of_echo_ipv6_entries;
	uint16_t   max_num_of_snmp_ipv4_entries;
	uint16_t   max_num_of_snmp_ipv6_entries;
	uint16_t   max_num_of_snmp_oid_entries;
	uint16_t   max_num_of_snmp_char; /* total amount of character needed
		for the snmp table */
	uint16_t   max_num_of_ip_prot_filtering;
	uint16_t   max_num_of_tcp_port_filtering;
	uint16_t   max_num_of_udp_port_filtering;
};
/* ARP */
struct   auto_res_arp_entry
{
	uint32_t  ip_address;
	uint8_t   mac[6];
	bool      is_vlan;
	uint16_t  vid;
};
struct   auto_res_arp_info
{
	uint8_t                     table_size;
	struct auto_res_arp_entry   *auto_res_table;
	bool                        enable_conflict_detection; /* when TRUE
		Conflict Detection will be checked and wake the host if
		needed */
};

/* NDP */
struct   auto_res_ndp_entry
{
	uint32_t  ip_address[4];
	uint8_t   mac[6];
	bool      is_vlan;
	uint16_t  vid;
};
struct   auto_res_ndp_info
{
	uint32_t                    multicast_group;
	uint8_t                     table_size_assigned;
	struct auto_res_ndp_entry   *auto_res_table_assigned; /* This list
		refer to solicitation IP addresses. Note that all IP adresses
		must be from the same multicast group. This will be checked and
		if not operation will fail. */
	uint8_t                     table_size_tmp;
	struct auto_res_ndp_entry   *auto_res_table_tmp;      /* This list
		refer to temp IP addresses. Note that all temp IP adresses must
		be from the same multicast group. This will be checked and if
		not operation will fail. */

	bool                        enable_conflict_detection; /* when TRUE
		Conflict Detection will be checked and wake the host if
		needed */
};

/* ICMP ECHO */
struct   auto_res_echo_ipv4_info
{
	uint8_t                     table_size;
	struct auto_res_arp_entry  *auto_res_table;
};

struct   auto_res_echo_ipv6_info
{
	uint8_t                     table_size;
	struct auto_res_ndp_entry  *auto_res_table;
};

/* SNMP */
struct   auto_res_snmp_entry
{
	uint16_t     oidSize;
	uint8_t      *oidVal; /* only the oid string */
	uint16_t     resSize;
	uint8_t      *resVal; /* resVal will be the entire reply,
				i.e. "Type|Length|Value" */
};

/**************************************************************************//**
 @Description   Deep Sleep Auto Response SNMP IPv4 Addresses Table Entry
                Refer to the FMan Controller spec for more details.
*//***************************************************************************/
struct auto_res_snmp_ipv4addr_tbl_entry
{
	uint32_t ipv4addr; /*!< 32 bit IPv4 Address. */
	bool      is_vlan;
	uint16_t vid;   /*!< 12 bits VLAN ID. The 4 left-most bits should be cleared                      */
			/*!< This field should be 0x0000 for an entry with no VLAN tag or a null VLAN ID. */
};

/**************************************************************************//**
 @Description   Deep Sleep Auto Response SNMP IPv6 Addresses Table Entry
                Refer to the FMan Controller spec for more details.
*//***************************************************************************/
struct auto_res_snmp_ipv6addr_tbl_entry
{
	uint32_t ipv6Addr[4];  /*!< 4 * 32 bit IPv6 Address.                                                     */
	bool      isVlan;
	uint16_t vid;       /*!< 12 bits VLAN ID. The 4 left-most bits should be cleared                      */
			/*!< This field should be 0x0000 for an entry with no VLAN tag or a null VLAN ID. */
};

struct   auto_res_snmp_info
{
	uint16_t control;                          /**< Control bits [0-15]. */
	uint16_t max_snmp_msg_length;              /**< Maximal allowed SNMP message length. */
	uint16_t num_ipv4_addresses;               /**< Number of entries in IPv4 addresses table. */
	uint16_t num_ipv6_addresses;               /**< Number of entries in IPv6 addresses table. */
	struct auto_res_snmp_ipv4addr_tbl_entry *ipv4addr_tbl; /**< Pointer to IPv4 addresses table. */
	struct auto_res_snmp_ipv6addr_tbl_entry *ipv6addr_tbl; /**< Pointer to IPv6 addresses table. */
	char                        *community_read_write_string;
	char                        *community_read_only_string;
	struct auto_res_snmp_entry  *oid_table;
	uint32_t                     oid_table_size;
	uint32_t                    *statistics;
};

/* Filtering */
struct   auto_res_port_filtering_entry
{
	uint16_t    src_port;
	uint16_t    dst_port;
	uint16_t    src_port_mask;
	uint16_t    dst_port_mask;
};
struct   auto_res_filtering_info
{
	/* IP protocol filtering parameters */
	uint8_t     ip_prot_table_size;
	uint8_t     *ip_prot_table_ptr;
	bool        ip_prot_pass_on_hit;  /* when TRUE, miss in the table will
		cause the packet to be droped, hit will pass the packet to
		UDP/TCP filters if needed and if not to the classification
		tree. If the classification tree will pass the packet to a
		queue it will cause a wake interupt. When FALSE it the other
		way around. */
	/* UDP port filtering parameters */
	uint8_t     udp_ports_table_size;
	struct auto_res_port_filtering_entry *udp_ports_table_ptr;
	bool        udp_port_pass_on_hit; /* when TRUE, miss in the table will
		cause the packet to be droped, hit will pass the packet to
		classification tree. If the classification tree will pass the
		packet to a queue it will cause a wake interupt. When FALSE it
		the other way around. */
    /* TCP port filtering parameters */
	uint16_t    tcp_flags_mask;
	uint8_t     tcp_ports_table_size;
	struct auto_res_port_filtering_entry *tcp_ports_table_ptr;
	bool        tcp_port_pass_on_hit; /* when TRUE, miss in the table will
		cause the packet to be droped, hit will pass the packet to
		classification tree. If the classification tree will pass the
		packet to a queue it will cause a wake interupt. When FALSE it
		the other way around. */
};

struct auto_res_port_params
{
	t_Handle                            h_FmPortTx;
	struct   auto_res_arp_info          *p_auto_res_arp_info;
	struct   auto_res_echo_ipv4_info    *p_auto_res_echo_ipv4_info;
	struct   auto_res_ndp_info          *p_auto_res_ndp_info;
	struct   auto_res_echo_ipv6_info    *p_auto_res_echo_ipv6_info;
	struct   auto_res_snmp_info         *p_auto_res_snmp_info;
	struct   auto_res_filtering_info    *p_auto_res_filtering_info;
};

struct auto_res_port_stats
{
    uint32_t arp_ar_cnt;
    uint32_t echo_icmpv4_ar_cnt;
    uint32_t ndp_ar_cnt;
    uint32_t echo_icmpv6_ar_cnt;
};

int fm_port_resume(struct fm_port *port);

int fm_port_suspend(struct fm_port *port);

#ifdef CONFIG_FMAN_PFC
/**************************************************************************//**
@Function     fm_port_set_pfc_priorities_mapping_to_qman_wq

@Description  Associate a QMan Work Queue with a PFC priority on this
		FM-port device (Tx port).

@Param[in]    port   - A handle of the FM port device.

@Param[in]    prio   - The PFC priority.

@Param[in]    wq   - The Work Queue associated with the PFC priority.

@Cautions     Allowed only after the port is initialized.
*//***************************************************************************/
int fm_port_set_pfc_priorities_mapping_to_qman_wq(struct fm_port *port,
		uint8_t prio, uint8_t wq);
#endif

/**************************************************************************//**
@Function     fm_mac_set_exception

@Description  Set MAC exception state.

@Param[in]    fm_mac_dev   - A handle of the FM MAC device.
@Param[in]    exception    - FM MAC exception type.
@Param[in]    enable       - new state.

*//***************************************************************************/
int fm_mac_set_exception(struct fm_mac_dev *fm_mac_dev,
		e_FmMacExceptions exception, bool enable);

int fm_mac_free(struct fm_mac_dev *fm_mac_dev);

struct fm_mac_dev *fm_mac_config(t_FmMacParams *params);

int fm_mac_config_max_frame_length(struct fm_mac_dev *fm_mac_dev,
		int len);

int fm_mac_config_pad_and_crc(struct fm_mac_dev *fm_mac_dev, bool enable);

int fm_mac_config_half_duplex(struct fm_mac_dev *fm_mac_dev, bool enable);

int fm_mac_config_reset_on_init(struct fm_mac_dev *fm_mac_dev, bool enable);

int fm_mac_init(struct fm_mac_dev *fm_mac_dev);

int fm_mac_get_version(struct fm_mac_dev *fm_mac_dev, uint32_t *version);

int fm_mac_enable(struct fm_mac_dev *fm_mac_dev);

int fm_mac_disable(struct fm_mac_dev *fm_mac_dev);

int fm_mac_resume(struct fm_mac_dev *fm_mac_dev);

int fm_mac_set_promiscuous(struct fm_mac_dev *fm_mac_dev,
		bool enable);

int fm_mac_remove_hash_mac_addr(struct fm_mac_dev *fm_mac_dev,
		t_EnetAddr *mac_addr);

int fm_mac_add_hash_mac_addr(struct fm_mac_dev *fm_mac_dev,
		t_EnetAddr *mac_addr);

int fm_mac_modify_mac_addr(struct fm_mac_dev *fm_mac_dev,
					const uint8_t *addr);

int fm_mac_adjust_link(struct fm_mac_dev *fm_mac_dev,
		bool link, int speed, bool duplex);

int fm_mac_enable_1588_time_stamp(struct fm_mac_dev *fm_mac_dev);

int fm_mac_disable_1588_time_stamp(struct fm_mac_dev *fm_mac_dev);

int fm_mac_set_rx_pause_frames(
		struct fm_mac_dev *fm_mac_dev, bool en);

int fm_mac_set_tx_pause_frames(struct fm_mac_dev *fm_mac_dev,
					     bool en);

#ifdef CONFIG_FSL_SDK_FMAN_RTC_API
int fm_rtc_enable(struct fm *fm_dev);

int fm_rtc_disable(struct fm *fm_dev);

int fm_rtc_get_cnt(struct fm *fm_dev, uint64_t *ts);

int fm_rtc_set_cnt(struct fm *fm_dev, uint64_t ts);

int fm_rtc_get_drift(struct fm *fm_dev, uint32_t *drift);

int fm_rtc_set_drift(struct fm *fm_dev, uint32_t drift);

int fm_rtc_set_alarm(struct fm *fm_dev, uint32_t id,
		uint64_t time);

int fm_rtc_set_fiper(struct fm *fm_dev, uint32_t id,
		uint64_t fiper);
#else
static inline int fm_rtc_enable(struct fm *fm_dev) { return 0; }

static inline int fm_rtc_disable(struct fm *fm_dev) { return 0; }

static inline int fm_rtc_get_cnt(struct fm *fm_dev, uint64_t *ts) { return 0; }

static inline int fm_rtc_set_cnt(struct fm *fm_dev, uint64_t ts) { return 0; }

static inline int fm_rtc_get_drift(struct fm *fm_dev, uint32_t *drift)
{ return 0; }

static inline int fm_rtc_set_drift(struct fm *fm_dev, uint32_t drift)
{ return 0; }

static inline int fm_rtc_set_alarm(struct fm *fm_dev, uint32_t id,
				   uint64_t time) { return 0; }

static inline int fm_rtc_set_fiper(struct fm *fm_dev, uint32_t id,
				   uint64_t fiper) { return 0; }
#endif

int fm_mac_set_wol(struct fm_port *port, struct fm_mac_dev *fm_mac_dev,
			bool en);

/** @} */ /* end of FM_LnxKern_ctrl_grp group */
/** @} */ /* end of FM_LnxKern_grp group */

/* default values for initializing PTP 1588 timer clock */
#define DPA_PTP_NOMINAL_FREQ_PERIOD_SHIFT 2 /* power of 2 for better performance */
#define DPA_PTP_NOMINAL_FREQ_PERIOD_NS (1 << DPA_PTP_NOMINAL_FREQ_PERIOD_SHIFT) /* 4ns,250MHz */

#endif /* __LNXWRP_FSL_FMAN_H */
