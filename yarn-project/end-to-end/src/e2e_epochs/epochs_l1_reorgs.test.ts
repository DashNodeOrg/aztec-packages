import { type AztecNode, type Logger, retryUntil } from '@aztec/aztec.js';
import { Blob } from '@aztec/blob-lib';
import { createBlobSinkClient } from '@aztec/blob-sink/client';
import type { ChainMonitor, Delayer } from '@aztec/ethereum/test';
import { hexToBuffer } from '@aztec/foundation/string';
import { executeTimeout } from '@aztec/foundation/timer';
import type { ProverNode } from '@aztec/prover-node';
import type { TestSequencerClient } from '@aztec/sequencer-client/test';

import { jest } from '@jest/globals';
import 'jest-extended';
import { keccak256, parseTransaction } from 'viem';

import type { EndToEndContext } from '../fixtures/utils.js';
import { EpochsTestContext, L1_BLOCK_TIME_IN_S, L2_SLOT_DURATION_IN_S } from './epochs_test.js';

jest.setTimeout(1000 * 60 * 10);

describe('e2e_epochs/epochs_l1_reorgs', () => {
  let context: EndToEndContext;
  let logger: Logger;
  let node: AztecNode;
  let proverNode: ProverNode;
  let monitor: ChainMonitor;
  let proverDelayer: Delayer;
  let sequencerDelayer: Delayer;

  let test: EpochsTestContext;

  beforeEach(async () => {
    test = await EpochsTestContext.setup({
      l1PublishRetryIntervalMS: 300_000, // Do not retry l1 txs, we dont want them to land
      txPropagationMaxQueryAttempts: 2, // We are blocking txs here, so do not spend much time looking for them
    });
    ({ proverDelayer, sequencerDelayer, context, logger, monitor } = test);
    node = context.aztecNode;
    proverNode = context.proverNode!;
  });

  afterEach(async () => {
    await test.teardown();
  });

  it('prunes L2 blocks if a proof is removed due to an L1 reorg', async () => {
    // Wait until we have proven something and the nodes have caught up
    logger.warn(`Waiting for initial proof to land`);
    const provenBlock = await test.waitUntilProvenL2BlockNumber(1);
    await retryUntil(() => node.getProvenBlockNumber().then(p => p >= provenBlock), 'node sync', 10, 0.1);

    // Stop the prover node so it doesn't re-submit the proof after we've removed it
    logger.warn(`Proof for block ${provenBlock} mined, stopping prover node`);
    await proverNode.stop();

    // And remove the proof from L1
    await context.cheatCodes.eth.reorg(2);
    await monitor.run();
    expect(monitor.l2ProvenBlockNumber).toEqual(0);

    // Wait until the end of the proof submission window for the first epoch
    await test.waitUntilEndOfProofSubmissionWindow(0);

    // Ensure that a new node sees the reorg
    logger.warn(`Syncing new node to test reorg`);
    const newNode = await executeTimeout(() => test.createNonValidatorNode(), 10_000, `new node sync`);
    expect(await newNode.getProvenBlockNumber()).toEqual(0);

    // Latest block number seen by the node may be the current one, or one less if it was *just* mined.
    // This is because the call to createNonValidatorNode will block until the initial sync is completed,
    // but the initial sync is done to the latest L1 block _at the time the initial sync starts_. So a new
    // node may have appeared while the initial sync runs, that's why we account for a small span of blocks.
    const currentBlock = monitor.l2BlockNumber;
    expect(await newNode.getBlockNumber()).toBeWithin(currentBlock - 1, currentBlock + 1);

    // And check that the old node has processed the reorg as well
    logger.warn(`Testing old node after reorg`);
    expect(await node.getProvenBlockNumber()).toEqual(0);
    expect(await node.getBlockNumber()).toBeWithin(currentBlock - 1, currentBlock + 1);

    logger.warn(`Test succeeded`);
    await newNode.stop();
  });

  it('does not prune if a second proof lands within the submission window after the first one is reorged out', async () => {
    // Wait until we have proven something and the nodes have caught up
    logger.warn(`Waiting for initial proof to land`);
    const provenBlock = await test.waitUntilProvenL2BlockNumber(1);
    await retryUntil(() => node.getProvenBlockNumber().then(p => p >= provenBlock), 'node sync', 10, 0.1);

    // Remove the proof from L1 but do not change the block number
    await context.cheatCodes.eth.reorgWithReplacement(1);
    await expect(monitor.run(true).then(m => m.l2ProvenBlockNumber)).resolves.toEqual(0);

    // Create another prover node so it submits a proof
    await test.createProverNode();

    // Wait until the end of the proof submission window for the first epoch
    await test.waitUntilEndOfProofSubmissionWindow(0);

    // And expect that the other node has submitted a proof
    await expect(monitor.run(true).then(m => m.l2ProvenBlockNumber)).resolves.toBeGreaterThanOrEqual(1);

    // Check that the node has followed along
    logger.warn(`Testing old node`);
    const currentBlock = monitor.l2BlockNumber;
    expect(await node.getProvenBlockNumber()).toBeGreaterThanOrEqual(1);
    expect(await node.getBlockNumber()).toBeWithin(currentBlock - 1, currentBlock + 1);

    logger.warn(`Test succeeded`);
  });

  it('restores L2 blocks if a proof is added due to an L1 reorg', async () => {
    // Next proof shall not land
    proverDelayer.cancelNextTx();

    // Expect pending chain to advance, so there's something to be pruned
    await retryUntil(() => node.getBlockNumber().then(b => b > 1), 'node sync', 60, 0.1);

    // Wait until the end of the proof submission window for the first epoch
    await test.waitUntilEndOfProofSubmissionWindow(0);
    await monitor.run(true);
    logger.warn(`End of epoch 0 submission window (L1 block ${await monitor.run(true).then(m => m.l1BlockNumber)}).`);

    // Grab the prover's tx to submit it later as part of a reorg and stop the prover
    const [proofTx] = proverDelayer.getCancelledTxs();
    expect(proofTx).toBeDefined();
    await proverNode.stop();

    // Wait for the node to prune
    const syncTimeout = L2_SLOT_DURATION_IN_S * 2;
    await retryUntil(() => node.getBlockNumber().then(b => b <= 1), 'node sync', syncTimeout, 0.1);
    expect(monitor.l2ProvenBlockNumber).toEqual(0);
    expect(await node.getProvenBlockNumber()).toEqual(0);

    // But not all is lost, for a reorg gets the proof back on chain!
    logger.warn(`Reorging proof back (L1 block ${await monitor.run(true).then(m => m.l1BlockNumber)}).`);
    await context.cheatCodes.eth.reorgWithReplacement(4, [[proofTx]]);
    const proofTxReceipt = await test.l1Client.getTransactionReceipt({ hash: keccak256(proofTx) });
    expect(proofTxReceipt.status).toEqual('success');

    // Monitor should update to see the proof
    await monitor.run(true);
    expect(monitor.l2BlockNumber).toBeGreaterThan(1);
    expect(monitor.l2ProvenBlockNumber).toBeGreaterThan(0);

    // And so the node undoes its reorg
    await retryUntil(() => node.getBlockNumber().then(b => b === monitor.l2BlockNumber), 'node sync', syncTimeout, 0.1);
    await expect(node.getProvenBlockNumber()).resolves.toEqual(monitor.l2ProvenBlockNumber);

    logger.warn(`Test succeeded`);
  });

  it('prunes L2 blocks from pending chain removed from L1 due to an L1 reorg', async () => {
    // Wait until L2_BLOCK_NUMBER is mined and node synced, and stop the sequencer
    const L2_BLOCK_NUMBER = 3;
    await test.waitUntilL2BlockNumber(L2_BLOCK_NUMBER, 60);
    expect(monitor.l2BlockNumber).toEqual(L2_BLOCK_NUMBER);
    const l1BlockNumber = monitor.l1BlockNumber;
    await retryUntil(() => node.getBlockNumber().then(b => b === L2_BLOCK_NUMBER), 'node sync', 10, 0.1);

    logger.warn(`Reached block ${L2_BLOCK_NUMBER}. Stopping block production.`);
    await context.aztecNodeAdmin!.setConfig({ minTxsPerBlock: 100 });

    // Remove the L2 block from L1
    const l1BlocksToReorg = monitor.l1BlockNumber - l1BlockNumber + 1;
    await context.cheatCodes.eth.reorgWithReplacement(l1BlocksToReorg);
    expect(await monitor.run(true).then(monitor => monitor.l2BlockNumber)).toEqual(L2_BLOCK_NUMBER - 1);
    logger.warn(`Removed block ${L2_BLOCK_NUMBER} via L1 reorg`);

    // And expect the node to prune the block
    await retryUntil(() => node.getBlockNumber().then(b => b === L2_BLOCK_NUMBER - 1), 'node sync', 30, 0.1);
  });

  it('sees new blocks added in an L1 reorg', async () => {
    // Wait until the block *before* L2_BLOCK_NUMBER is mined and node synced
    const L2_BLOCK_NUMBER = 3;
    await test.waitUntilL2BlockNumber(L2_BLOCK_NUMBER - 1, 60);
    expect(monitor.l2BlockNumber).toEqual(L2_BLOCK_NUMBER - 1);
    const l1BlockNumber = monitor.l1BlockNumber;
    await retryUntil(() => node.getBlockNumber().then(b => b === L2_BLOCK_NUMBER - 1), 'node sync', 5, 0.1);

    // Cancel the next tx to be mined and pause the sequencer
    sequencerDelayer.cancelNextTx();
    await retryUntil(() => sequencerDelayer.getCancelledTxs().length, 'next block tx', L2_SLOT_DURATION_IN_S * 2, 0.1);
    const [l2BlockTx] = sequencerDelayer.getCancelledTxs();
    await context.aztecNodeAdmin!.setConfig({ minTxsPerBlock: 100 });

    // Wait until a few more L1 blocks go by, and trigger a reorg including the target block
    await retryUntil(() => monitor.l1BlockNumber > l1BlockNumber + 2, 'l1 block number', L1_BLOCK_TIME_IN_S * 4, 0.1);
    expect(await node.getBlockNumber()).toEqual(L2_BLOCK_NUMBER - 1);
    logger.warn(`Triggering ${monitor.l1BlockNumber - l1BlockNumber}-block L1 reorg to include L2 block`);

    // Note that we cannot use reorgWithReplacement here for the reorg, due to an anvil bug with blob txs,
    // so we reorg, then replay the tx, and then mine. We also run a simulate before so we can extract any
    // errors that would arise if the tx would revert, to assist with debugging.
    await context.cheatCodes.eth.reorg(monitor.l1BlockNumber - l1BlockNumber);
    await context.cheatCodes.eth.mine(2);
    const l2BlockTxRequest = parseTransaction(l2BlockTx);
    const sender = (context.sequencer as TestSequencerClient).sequencer.publisher.getSenderAddress();
    const result = await test.l1Client.simulateCalls({ account: sender.toString(), calls: [l2BlockTxRequest as any] });
    logger.warn(`Simulation result for L2 block tx`, result);
    expect(result.results[0].error).toBeUndefined();
    expect(result.results[0].status).toEqual('success');
    logger.warn(`Sending L2 block tx to L1`);
    const txHash = await test.l1Client.sendRawTransaction({ serializedTransaction: l2BlockTx });
    await context.cheatCodes.eth.mine(1);

    // Check that the tx was reorged in and succeeded
    const txReceipt = await test.l1Client.getTransactionReceipt({ hash: txHash });
    expect(txReceipt.status).toEqual('success');
    expect(txReceipt.blobGasUsed).toBeGreaterThan(0n);
    expect(await monitor.run(true).then(m => m.l2BlockNumber)).toEqual(L2_BLOCK_NUMBER);

    // We also need to send the blob to the sink, so the node can get it
    logger.warn(`Sending blobs to blob sink`);
    const blobs = await getBlobs(l2BlockTx);
    const blobSinkClient = createBlobSinkClient(context.config);
    await blobSinkClient.sendBlobsToBlobSink(txReceipt.blockHash, blobs);

    // And wait for the node to see the new block
    await retryUntil(() => node.getBlockNumber().then(b => b === L2_BLOCK_NUMBER), 'node sync', 5, 0.1);
  });

  // TODO(palla/reorg): Archiver keeps a different tracker for the L1 block number for cross chain
  // messages, so even if it works fine when reorging blocks, it may still fail on messages.
  it.skip('updates cross-chain messages changed due to an L1 reorg', async () => {});
});

function getBlobs(serializedTx: `0x${string}`) {
  const parsedTx = parseTransaction(serializedTx);
  if (parsedTx.sidecars === false) {
    throw new Error('No sidecars found in tx');
  }
  return Promise.all(parsedTx.sidecars!.map(sidecar => Blob.fromEncodedBlobBuffer(hexToBuffer(sidecar.blob))));
}
