/*
 * Copyright (c) 2012-2018 The Linux Foundation. All rights reserved.
 *
 * Previously licensed under the ISC license by Qualcomm Atheros, Inc.
 *
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This file was originally distributed by Qualcomm Atheros, Inc.
 * under proprietary terms before Copyright ownership was assigned
 * to the Linux Foundation.
 */

#if !defined( __LIM_SESSION_H )
#define __LIM_SESSION_H

#ifdef WLAN_FEATURE_FILS_SK
#include "lim_fils_defs.h"
#endif
/**=========================================================================

  \file  limSession.h

  \brief prototype for lim Session related APIs

  \author Sunit Bhatia

  ========================================================================*/

/* Powersave Offload Implementation */
typedef enum ePowersaveState
{
    PMM_FULL_POWER,
    PMM_POWER_SAVE
}tPowersaveState;

/* Master Structure: This will be part of PE Session Entry */
typedef struct sPowersaveoffloadInfo
{
    tPowersaveState psstate;
    tANI_U8 bcnmiss;
}tPowersaveoffloadInfo, tpPowersaveoffloadInfo;

#ifdef WLAN_FEATURE_11W
/*
 * This struct is needed for handling of ASSOC RSP with TRY AGAIN LATER
 * It stores the context and state machine setting for MLM so that it can
 * go back send ASSOC REQ frame again after the timer has expired.
 */
typedef struct tagComebackTimerInfo
{
    tpAniSirGlobal   pMac;
    tANI_U8          sessionID;
    tLimMlmStates    limPrevMlmState;   /* Previous MLM State */
    tLimSmeStates    limMlmState;       /* MLM State */
} tComebackTimerInfo;
#endif /* WLAN_FEATURE_11W */

/*--------------------------------------------------------------------------
  Include Files
  ------------------------------------------------------------------------*/



/*--------------------------------------------------------------------------
  Preprocessor definitions and constants
  ------------------------------------------------------------------------*/
#define NUM_WEP_KEYS 4

// Maximum allowable size of a beacon frame
#define SCH_MAX_BEACON_SIZE    512

#define SCH_MAX_PROBE_RESP_SIZE 512

#define SCH_PROTECTION_RESET_TIME 4000
/*--------------------------------------------------------------------------
  Type declarations
  ------------------------------------------------------------------------*/
typedef struct
{
    tSirMacBeaconInterval   beaconInterval;
    tANI_U8                 fShortPreamble;
    tANI_U8                 llaCoexist;
    tANI_U8                 llbCoexist;
    tANI_U8                 llgCoexist;
    tANI_U8                 ht20Coexist;
    tANI_U8                 llnNonGFCoexist;
    tANI_U8                 fRIFSMode;
    tANI_U8                 fLsigTXOPProtectionFullSupport;
    tANI_U8                 gHTObssMode;
}tBeaconParams, *tpBeaconParams;

typedef struct aid {
	struct mutex lock;
	bool valid;
	uint8_t aid;
	struct completion timeout;
} aid_t;

typedef struct sPESession           // Added to Support BT-AMP
{
    /* To check session table is in use or free*/
    tANI_U8                 available;
    tANI_U16                peSessionId;
    tANI_U8                 smeSessionId;
    tANI_U16                transactionId;

    //In AP role: BSSID and selfMacAddr will be the same.
    //In STA role: they will be different
    tSirMacAddr             bssId;
    tSirMacAddr             selfMacAddr;
    tSirMacSSid             ssId;
    tANI_U8                 bssIdx;
    tANI_U8                 valid;
    tLimMlmStates           limMlmState;            //MLM State
    tLimMlmStates           limPrevMlmState;        //Previous MLM State
    tLimSmeStates           limSmeState;            //SME State
    tLimSmeStates           limPrevSmeState;        //Previous SME State
    tLimSystemRole          limSystemRole;
    tSirBssType             bssType;
    tANI_U8                 operMode;               // AP - 0; STA - 1 ;
    tSirNwType              nwType;
    tpSirSmeStartBssReq     pLimStartBssReq;        //handle to smestart bss req
    bool                    osen_association;
    bool                    wps_registration;
    tpSirSmeJoinReq         pLimJoinReq;            // handle to sme join req
    tpSirSmeJoinReq         pLimReAssocReq;         //handle to sme reassoc req
    tpLimMlmJoinReq         pLimMlmJoinReq;         //handle to MLM join Req
#if  defined (WLAN_FEATURE_VOWIFI_11R) || defined (FEATURE_WLAN_ESE) || defined(FEATURE_WLAN_LFR)
    void                    *pLimMlmReassocRetryReq; //keep reasoc req for retry
#endif
    void                    *pLimMlmReassocReq;      //handle to MLM reassoc Req
    tANI_U16                channelChangeReasonCode;
    tANI_U8                 dot11mode;
    tANI_U8                 htCapability;
    /* Supported Channel Width Set: 0-20MHz 1 - 40MHz */
    tANI_U8                 htSupportedChannelWidthSet;
    /* Recommended Tx Width Set
     * 0 - use 20 MHz channel (control channel)
     * 1 - use channel width enabled under Supported Channel Width Set
     */
    tANI_U8                 htRecommendedTxWidthSet;
    /* Identifies the 40 MHz extension channel */
    ePhyChanBondState       htSecondaryChannelOffset;
    tSirRFBand              limRFBand;
    tANI_U8                 limIbssActive;          //TO SUPPORT CONCURRENCY

    /* These global varibales moved to session Table to support BT-AMP : Oct 9th review */
    tAniAuthType            limCurrentAuthType;
    tANI_U16                limCurrentBssCaps;
    tANI_U8                 limCurrentBssQosCaps;
    tANI_U16                limCurrentBssPropCap;
    tANI_U8                 limSentCapsChangeNtf;
    tANI_U16                limAID;

    /* Parameters  For Reassociation */
    tSirMacAddr             limReAssocbssId;
    tSirMacChanNum          limReassocChannelId;
    /* CB paramaters required/duplicated for Reassoc since re-assoc mantains its own params in lim */
    tANI_U8                 reAssocHtSupportedChannelWidthSet;
    tANI_U8                 reAssocHtRecommendedTxWidthSet;
    ePhyChanBondState       reAssocHtSecondaryChannelOffset;
    tSirMacSSid             limReassocSSID;
    tANI_U16                limReassocBssCaps;
    tANI_U8                 limReassocBssQosCaps;
    tANI_U16                limReassocBssPropCap;

    // Assoc or ReAssoc Response Data/Frame
    void                   *limAssocResponseData;

    /** BSS Table parameters **/

    /*
     * staId:  Start BSS: this is the  Sta Id for the BSS.
     * Join: this is the selfStaId
     * In both cases above, the peer STA ID wll be stored in dph hash table.
     */
    tANI_U16                staId;
    tANI_U16                statypeForBss; //to know session is for PEER or SELF
    tANI_U8                 shortSlotTimeSupported;
    tANI_U8                 dtimPeriod;
    tSirMacRateSet          rateSet;
    tSirMacRateSet          extRateSet;
    tSirMacHTOperatingMode  htOperMode;
    tANI_U8                 currentOperChannel;
    tANI_U8                 currentReqChannel;
    tANI_U8                 LimRxedBeaconCntDuringHB;

    //Time stamp of the last beacon received from the BSS to which STA is connected.
    tANI_U64                lastBeaconTimeStamp;
    //RX Beacon count for the current BSS to which STA is connected.
    tANI_U32                currentBssBeaconCnt;
    tANI_U8                 lastBeaconDtimCount;
    tANI_U8                 lastBeaconDtimPeriod;

    tANI_U32                bcnLen;
    tANI_U8                 *beacon;                //Used to store last beacon / probe response before assoc.

    tANI_U32                assocReqLen;
    tANI_U8                 *assocReq;              //Used to store association request frame sent out while associating.

    tANI_U32                assocRspLen;
    tANI_U8                 *assocRsp;              //Used to store association response received while associating
    tAniSirDph              dph;
    void *                  *parsedAssocReq;        //Used to store parsed assoc req from various requesting station
#ifdef WLAN_FEATURE_VOWIFI_11R
    tANI_U32                RICDataLen;             //Used to store the Ric data received in the assoc response
    tANI_U8                 *ricData;
#endif
#ifdef FEATURE_WLAN_ESE
    tANI_U32                tspecLen;               //Used to store the TSPEC IEs received in the assoc response
    tANI_U8                 *tspecIes;
#endif
    tANI_U32                encryptType;

    tANI_BOOLEAN            bTkipCntrMeasActive;    // Used to keep record of TKIP counter measures start/stop

    tANI_U8                 gLimProtectionControl;  //used for 11n protection

    tANI_U8                 gHTNonGFDevicesPresent;

    //protection related config cache
    tCfgProtection          cfgProtection;

    // Number of legacy STAs associated
    tLimProtStaParams          gLim11bParams;

    // Number of 11A STAs associated
    tLimProtStaParams          gLim11aParams;

    // Number of non-ht non-legacy STAs associated
    tLimProtStaParams          gLim11gParams;

    //Number of nonGf STA associated
    tLimProtStaParams       gLimNonGfParams;

    //Number of HT 20 STAs associated
    tLimProtStaParams          gLimHt20Params;

    //Number of Lsig Txop not supported STAs associated
    tLimProtStaParams          gLimLsigTxopParams;

    // Number of STAs that do not support short preamble
    tLimNoShortParams         gLimNoShortParams;

    // Number of STAs that do not support short slot time
    tLimNoShortSlotParams   gLimNoShortSlotParams;


    // OLBC parameters
    tLimProtStaParams  gLimOlbcParams;

    // OLBC parameters
    tLimProtStaParams  gLimOverlap11gParams;

    tLimProtStaParams  gLimOverlap11aParams;
    tLimProtStaParams gLimOverlapHt20Params;
    tLimProtStaParams gLimOverlapNonGfParams;

    //cache for each overlap
    tCacheParams     protStaCache[LIM_PROT_STA_CACHE_SIZE];

    tANI_U8                 privacy;
    tAniAuthType            authType;
    tSirKeyMaterial         WEPKeyMaterial[NUM_WEP_KEYS];

    tDot11fIERSN            gStartBssRSNIe;
    tDot11fIEWPA            gStartBssWPAIe;
    tSirAPWPSIEs            APWPSIEs;
    tANI_U8                 apUapsdEnable;
    tSirWPSPBCSession       *pAPWPSPBCSession;
    tANI_U32                DefProbeRspIeBitmap[8];
    tANI_U32                proxyProbeRspEn;
    tDot11fProbeResponse    probeRespFrame;
    tANI_U8                 ssidHidden;
    tANI_BOOLEAN            fwdWPSPBCProbeReq;
    tANI_U8                 wps_state;

    tANI_U8            limQosEnabled:1; //11E
    tANI_U8            limWmeEnabled:1; //WME
    tANI_U8            limWsmEnabled:1; //WSM
    tANI_U8            limHcfEnabled:1;
    tANI_U8            lim11dEnabled:1;
#ifdef WLAN_FEATURE_11W
    tANI_U8            limRmfEnabled:1; //11W
#endif
    tANI_U32           lim11hEnable;

    tPowerdBm  maxTxPower;   //MIN (Regulatory and local power constraint)
    tVOS_CON_MODE      pePersona;
#if defined WLAN_FEATURE_VOWIFI
    tPowerdBm  txMgmtPower;
#endif

#ifdef WLAN_FEATURE_VOWIFI_11R
    tAniBool            is11Rconnection;
#endif

#ifdef FEATURE_WLAN_ESE
    tAniBool            isESEconnection;
    tEsePEContext       eseContext;
#endif
#if defined WLAN_FEATURE_VOWIFI_11R || defined FEATURE_WLAN_ESE || defined(FEATURE_WLAN_LFR)
    tAniBool            isFastTransitionEnabled;
#endif
#ifdef FEATURE_WLAN_LFR
    tAniBool            isFastRoamIniFeatureEnabled;
#endif
    tSirNoAParam p2pNoA;
    tSirP2PNoaAttr p2pGoPsUpdate;
    tANI_U32 defaultAuthFailureTimeout;
    tSirP2PNoaStart p2pGoPsNoaStartInd;

    /* EDCA QoS parameters
     * gLimEdcaParams - These EDCA parameters are used locally on AP or STA.
     * If STA, then these are values taken from the Assoc Rsp when associating,
     * or Beacons/Probe Response after association.  If AP, then these are
     * values originally set locally on AP.
     *
     * gLimEdcaParamsBC - These EDCA parameters are use by AP to broadcast
     * to other STATIONs in the BSS.
     *
     * gLimEdcaParamsActive: These EDCA parameters are what's actively being
     * used on station. Specific AC values may be downgraded depending on
     * admission control for that particular AC.
     */
    tSirMacEdcaParamRecord gLimEdcaParams[MAX_NUM_AC];   //used locally
    tSirMacEdcaParamRecord gLimEdcaParamsBC[MAX_NUM_AC]; //used for broadcast
    tSirMacEdcaParamRecord gLimEdcaParamsActive[MAX_NUM_AC];

    tANI_U8  gLimEdcaParamSetCount;

    tBeaconParams beaconParams;
#ifdef WLAN_FEATURE_11AC
    tANI_U8 vhtCapability;
    tANI_U8 vhtTxChannelWidthSet;
    tLimOperatingModeInfo  gLimOperatingMode;
    tANI_U8    vhtCapabilityPresentInBeacon;
    tANI_U8    apCenterChan;
    tANI_U8    apChanWidth;
    tANI_U8    txBFIniFeatureEnabled;
    tANI_U8    txMuBformee;
    tANI_U8    enableVhtpAid;
    tANI_U8    enableVhtGid;
#endif
    tLimWiderBWChannelSwitchInfo  gLimWiderBWChannelSwitch;
    tANI_U8    enableAmpduPs;
    tANI_U8    enableHtSmps;
    tANI_U8    htSmpsvalue;
    tANI_U8            spectrumMgtEnabled;
    /* *********************11H related*****************************/
    tLimSpecMgmtInfo   gLimSpecMgmt;
    // CB Primary/Secondary Channel Switch Info
    tLimChannelSwitchInfo  gLimChannelSwitch;
    /* *********************End 11H related*****************************/

    uint8_t    lim_sub20_channel_switch_bandwidth;

    /*Flag to Track Status/Indicate HBFailure on this session */
    tANI_BOOLEAN LimHBFailureStatus;
    tANI_U32           gLimPhyMode;
    tANI_U8            amsduSupportedInBA;
    tANI_U8          txLdpcIniFeatureEnabled;
    /**
     * Following is the place holder for free peer index pool.
     * A non-zero value indicates that peer index is available
     * for assignment.
     */
    tANI_U8    *gpLimPeerIdxpool;
    tANI_U8    freePeerIdxHead;
    tANI_U8    freePeerIdxTail;
    tANI_U16  gLimNumOfCurrentSTAs;
#ifdef FEATURE_WLAN_TDLS
    tANI_U32  peerAIDBitmap[2];
    bool tdls_prohibited;
    bool tdls_chan_swit_prohibited;
#endif
    tANI_BOOLEAN fWaitForProbeRsp;
    tANI_BOOLEAN fIgnoreCapsChange;
    tANI_BOOLEAN fDeauthReceived;
#ifdef FEATURE_WLAN_DIAG_SUPPORT_LIM
    tANI_S8 rssi;
#endif
    tANI_U8 max_amsdu_num;
    tANI_U8 isCoalesingInIBSSAllowed;

    tSirHTConfig htConfig;

    /*
     * Place holder for StartBssReq message
     * received by SME state machine
     */
    tANI_U8 gLimCurrentBssUapsd;

    /* Used on STA, this is a static UAPSD mask setting
     * derived from SME_JOIN_REQ and SME_REASSOC_REQ. If a
     * particular AC bit is set, it means the AC is both
     * trigger enabled and delivery enabled.
     */
    tANI_U8  gUapsdPerAcBitmask;

    /* Used on STA, this is a dynamic UPASD mask setting
     * derived from AddTS Rsp and DelTS frame. If a
     * particular AC bit is set, it means AC is trigger
     * enabled.
     */
    tANI_U8  gUapsdPerAcTriggerEnableMask;

    /* Used on STA, dynamic UPASD mask setting
     * derived from AddTS Rsp and DelTs frame. If
     * a particular AC bit is set, it means AC is
     * delivery enabled.
     */
    tANI_U8  gUapsdPerAcDeliveryEnableMask;

    /* Flag to skip CSA IE processing when CSA
     * offload is enabled.
     */
    tANI_U8  csaOffloadEnable;

    /* Used on STA for AC downgrade. This is a dynamic mask
     * setting which keep tracks of ACs being admitted.
     * If bit is set to 0: That partiular AC is not admitted
     * If bit is set to 1: That particular AC is admitted
     */
    tANI_U8  gAcAdmitMask[SIR_MAC_DIRECTION_DIRECT];

    /* Power Save Off load Parameters */
    tPowersaveoffloadInfo pmmOffloadInfo;

    /* SMPS mode */
    tANI_U8  smpsMode;

    tANI_U8  chainMask;

    /* Flag to indicate Chan Sw announcement is required */
    tANI_U8  dfsIncludeChanSwIe;

    /* Flag to indicate Chan Wrapper Element is required */
    tANI_U8  dfsIncludeChanWrapperIe;

#ifdef FEATURE_WLAN_MCC_TO_SCC_SWITCH
    tANI_U8  cc_switch_mode;
#endif

    tANI_BOOLEAN isCiscoVendorAP;

    tSirAddIeParams     addIeParams;

    tANI_U8 *pSchProbeRspTemplate;
    // Beginning portion of the beacon frame to be written to TFP
    tANI_U8 *pSchBeaconFrameBegin;
    // Trailing portion of the beacon frame to be written to TFP
    tANI_U8 *pSchBeaconFrameEnd;
    // Size of the beginning portion
    tANI_U16 schBeaconOffsetBegin;
    // Size of the trailing portion
    tANI_U16 schBeaconOffsetEnd;
    tANI_BOOLEAN isOSENConnection;
    /*  DSCP to UP mapping for HS 2.0 */
    tSirQosMapSet QosMapSet;

#ifdef WLAN_FEATURE_ROAM_OFFLOAD
    tANI_BOOLEAN bRoamSynchInProgress;
#endif

#if defined WLAN_FEATURE_VOWIFI_11R
    /* Fast Transition (FT) */
    tftPEContext  ftPEContext;
#endif
    tANI_BOOLEAN            isNonRoamReassoc;
#ifdef WLAN_FEATURE_11W
    vos_timer_t pmfComebackTimer;
    tComebackTimerInfo pmfComebackTimerInfo;
#endif /* WLAN_FEATURE_11W */
    tANI_U8  isKeyInstalled;
    /* timer for reseting protection fileds at regular intervals */
    vos_timer_t protection_fields_reset_timer;
    void *mac_ctx;
    /*
     * variable to store state of various protection struct like
     * gLimOlbcParams, gLimOverlap11gParams, gLimOverlapHt20Params etc
     */
    tANI_U16 old_protection_state;
    tSirMacAddr             prev_ap_bssid;
#ifdef FEATURE_AP_MCC_CH_AVOIDANCE
    /* tells if Q2Q IE, from another MDM device in AP MCC mode was recvd */
    bool sap_advertise_avoid_ch_ie;
#endif /* FEATURE_AP_MCC_CH_AVOIDANCE */
    uint8_t sap_dot11mc;
#ifdef FEATURE_WLAN_ESE
    uint8_t is_ese_version_ie_present;
#endif
    /* HS 2.0 Indication */
    tDot11fIEhs20vendor_ie hs20vendor_ie;
    /* flag to indicate country code in beacon */
    tANI_U8 countryInfoPresent;
    uint8_t vdev_nss;
    bool roaming_in_progress;
    bool add_bss_failed;
    /* Supported NSS is intersection of self and peer NSS */
    bool supported_nss_1x1;
    bool is_ext_caps_present;
    tDot11fIEHTCaps ht_caps;
    tDot11fIEVHTCaps vht_caps;
    tDot11fIEHTInfo ht_operation;
    tDot11fIEVHTOperation vht_operation;
    bool is_vendor_specific_vhtcaps;
    uint8_t vendor_specific_vht_ie_type;
    uint8_t vendor_specific_vht_ie_sub_type;
    bool vendor_vht_for_24ghz_sap;
    uint16_t beacon_tx_rate;
    uint8_t *access_policy_vendor_ie;
    uint8_t access_policy;
    uint8_t sap_sub20_channelwidth;
    uint8_t sub20_channelwidth;
    uint8_t vht_channel_width;
    /* Number of STAs that do not support ECSA capability */
    uint8_t lim_non_ecsa_cap_num;
    uint32_t sta_auth_retries_for_code17;
    bool force_24ghz_in_ht20;
#ifdef WLAN_FEATURE_FILS_SK
    struct pe_fils_session *fils_info;
#endif
    uint8_t deauthmsgcnt;
    uint8_t disassocmsgcnt;
    bool sae_pmk_cached;
    /* previous auth frame's sequence number */
    uint16_t prev_auth_seq_num;
    aid_t aid;
    bool aid_by_user;
} tPESession, *tpPESession;

/*-------------------------------------------------------------------------
  Function declarations and documenation
  ------------------------------------------------------------------------*/


/*--------------------------------------------------------------------------

  \brief peCreateSession() - creates a new PE session given the BSSID

  This function returns the session context and the session ID if the
  session corresponding to the passed BSSID is found in the PE session
  table.

  \param pMac                  - pointer to global adapter context
  \param bssid                 - BSSID of the new session
  \param sessionId             - session ID is returned here, if session is
                                 created.
  \param bssType               - bss type of new session to do conditional
                                 memory allocation.
  \return tpPESession          - pointer to the session context or NULL if
                                 session can not be created.

  \sa

  --------------------------------------------------------------------------*/
tpPESession peCreateSession(tpAniSirGlobal pMac,
                            tANI_U8 *bssid,
                            tANI_U8* sessionId,
                            tANI_U16 numSta,
                            tSirBssType bssType);

/*--------------------------------------------------------------------------
  \brief peFindSessionByBssid() - looks up the PE session given the BSSID.

  This function returns the session context and the session ID if the session
  corresponding to the given BSSID is found in the PE session table.

  \param pMac                   - pointer to global adapter context
  \param bssid                   - BSSID of the session
  \param sessionId             -session ID is returned here, if session is found.

  \return tpPESession          - pointer to the session context or NULL if session is not found.

  \sa
  --------------------------------------------------------------------------*/
tpPESession peFindSessionByBssid(tpAniSirGlobal pMac,  tANI_U8*  bssid,    tANI_U8* sessionId);



/*--------------------------------------------------------------------------
  \brief peFindSessionByBssIdx() - looks up the PE session given the bssIdx.

  This function returns the session context  if the session
  corresponding to the given bssIdx is found in the PE session table.
   \param pMac                   - pointer to global adapter context
  \param bssIdx                   - bss index of the session
  \return tpPESession          - pointer to the session context or NULL if session is not found.
  \sa
  --------------------------------------------------------------------------*/
tpPESession peFindSessionByBssIdx(tpAniSirGlobal pMac,  tANI_U8 bssIdx);




/*--------------------------------------------------------------------------
  \brief peFindSessionByPeerSta() - looks up the PE session given the Peer Station Address.

  This function returns the session context and the session ID if the session
  corresponding to the given destination address is found in the PE session table.

  \param pMac                   - pointer to global adapter context
  \param sa                   - Peer STA Address of the session
  \param sessionId             -session ID is returned here, if session is found.

  \return tpPESession          - pointer to the session context or NULL if session is not found.

  \sa
  --------------------------------------------------------------------------*/
tpPESession peFindSessionByPeerSta(tpAniSirGlobal pMac, tANI_U8*  sa, tANI_U8* sessionId);

/*--------------------------------------------------------------------------
  \brief peFindSessionBySessionId() - looks up the PE session given the session ID.

  This function returns the session context  if the session
  corresponding to the given session ID is found in the PE session table.

  \param pMac                   - pointer to global adapter context
  \param sessionId             -session ID for which session context needs to be looked up.

  \return tpPESession          - pointer to the session context or NULL if session is not found.

  \sa
  --------------------------------------------------------------------------*/
 tpPESession peFindSessionBySessionId(tpAniSirGlobal pMac , tANI_U8 sessionId);

/*--------------------------------------------------------------------------
  \brief peFindSessionByBssid() - looks up the PE session given staid.

  This function returns the session context and the session ID if the session
  corresponding to the given StaId is found in the PE session table.

  \param pMac                  - pointer to global adapter context
  \param staid                 - StaId of the session
  \param sessionId             - session ID is returned here, if session is found.

  \return tpPESession          - pointer to the session context or NULL if session is not found.

--------------------------------------------------------------------------*/
 tpPESession peFindSessionByStaId(tpAniSirGlobal pMac,  tANI_U8  staid,    tANI_U8* sessionId);





/*--------------------------------------------------------------------------
  \brief peDeleteSession() - deletes the PE session given the session ID.


  \param pMac                   - pointer to global adapter context
  \param sessionId             -session ID of the session which needs to be deleted.

  \sa
  --------------------------------------------------------------------------*/
void peDeleteSession(tpAniSirGlobal pMac, tpPESession psessionEntry);

/**
 * pe_find_session_by_sme_session_id() - looks up the PE session for given sme
 * session id
 * @mac_ctx:          pointer to global adapter context
 * @sme_session_id:   sme session id
 *
 * looks up the PE session for given sme session id
 *
 * Return: pe session entry for given sme session if found else NULL
 */
tpPESession pe_find_session_by_sme_session_id(tpAniSirGlobal mac_ctx,
					   uint8_t sme_session_id);
uint8_t pe_count_session_with_sme_session_id(tpAniSirGlobal mac_ctx,
	uint8_t sme_session_id);

int pe_get_active_session_count(tpAniSirGlobal mac_ctx);
#ifdef WLAN_FEATURE_FILS_SK
/**
 * pe_delete_fils_info: API to delete fils session info
 * @session: pe session
 *
 * Return: void
 */
void pe_delete_fils_info(tpPESession session);
#endif
#endif //#if !defined( __LIM_SESSION_H )
