// Copyright (c) 2017-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tx_verify.h"

#include "consensus/consensus.h"
#include "../validation.h"

bool IsFinalTx(const CTransactionRef& tx, int nBlockHeight, int64_t nBlockTime)
{
    // Time based nLockTime implemented in 0.1.6
    if (tx->nLockTime == 0)
        return true;
    if (nBlockTime == 0)
        nBlockTime = GetAdjustedTime();
    if ((int64_t)tx->nLockTime < ((int64_t)tx->nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    for (const CTxIn& txin : tx->vin)
        if (!txin.IsFinal())
            return false;
    return true;
}

unsigned int GetLegacySigOpCount(const CTransaction& tx)
{
    unsigned int nSigOps = 0;
    for (const CTxIn& txin : tx.vin) {
        nSigOps += txin.scriptSig.GetSigOpCount(false);
    }
    for (const CTxOut& txout : tx.vout) {
        nSigOps += txout.scriptPubKey.GetSigOpCount(false);
    }
    return nSigOps;
}

unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& inputs)
{
    if (tx.IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const CTxOut& prevout = inputs.AccessCoin(tx.vin[i].prevout).out;
        if (prevout.scriptPubKey.IsPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(tx.vin[i].scriptSig);
    }
    return nSigOps;
}

bool CheckTransaction(const CTransaction& tx, CValidationState& state, bool fColdStakingActive)
{
    // Basic checks that don't depend on any context
    // Transactions containing empty `vin` must have non-empty `vShieldedSpend`.
    if (tx.vin.empty() && (tx.sapData && tx.sapData->vShieldedSpend.empty()))
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vin-empty");
    // Transactions containing empty `vout` must have non-empty `vShieldedOutput`.
    if (tx.vout.empty() && (tx.sapData && tx.sapData->vShieldedOutput.empty()))
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vout-empty");

    // Version check
    if (tx.nVersion < 1 || tx.nVersion >= CTransaction::TxVersion::TOOHIGH) {
        return state.DoS(10,
                error("%s: Transaction version (%d) too high. Max: %d", __func__, tx.nVersion, int(CTransaction::TxVersion::TOOHIGH) - 1),
                REJECT_INVALID, "bad-tx-version-too-high");
    }

    // Size limits
    if (tx.GetTotalSize() > MAX_STANDARD_TX_SIZE) {
        return state.DoS(10, error("tx oversize: %d > %d", tx.GetTotalSize(), nMaxSize), REJECT_INVALID, "bad-txns-oversize");
    }

    // Dispatch to Sapling validator
    CAmount nValueOut = 0;
    if (!SaplingValidation::CheckTransaction(tx, state, nValueOut)) {
        return false;
    }

    // Check for negative or overflow output values
    const Consensus::Params& consensus = Params().GetConsensus();
    for (const CTxOut& txout : tx.vout) {
        if (txout.IsEmpty() && !tx.IsCoinBase() && !tx.IsCoinStake())
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-empty");
        if (txout.nValue < 0)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-negative");
        if (txout.nValue > consensus.nMaxMoneyOut)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-toolarge");
        nValueOut += txout.nValue;
        if (!consensus.MoneyRange(nValueOut))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge");
        // check cold staking enforcement (for delegations) and value out
        if (txout.scriptPubKey.IsPayToColdStaking()) {
            if (!fColdStakingActive)
                return state.DoS(10, false, REJECT_INVALID, "cold-stake-inactive");
            if (txout.nValue < MIN_COLDSTAKING_AMOUNT)
                return state.DoS(100, false, REJECT_INVALID, "cold-stake-vout-toosmall");
        }
    }

    std::set<COutPoint> vInOutPoints;
    for (const CTxIn& txin : tx.vin) {
        // Check for duplicate inputs
        if (vInOutPoints.count(txin.prevout))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-duplicate");
        if (true) {
            vInOutPoints.insert(txin.prevout);
        }
    }

    if (tx.IsCoinBase()) {
        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 150)
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-length");
    } else {
        for (const CTxIn& txin : tx.vin)
            if (txin.prevout.IsNull())
                return state.DoS(10, false, REJECT_INVALID, "bad-txns-prevout-null");
    }

    return true;
}
