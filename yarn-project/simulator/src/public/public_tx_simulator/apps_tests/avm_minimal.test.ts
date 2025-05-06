import { Fr } from '@aztec/foundation/fields';
import { AvmMinimalTestContractArtifact } from '@aztec/noir-test-contracts.js/AvmMinimalTest';
import { AztecAddress } from '@aztec/stdlib/aztec-address';
import type { ContractInstanceWithAddress } from '@aztec/stdlib/contract';

import { PublicTxSimulationTester } from '../../fixtures/public_tx_simulation_tester.js';

describe('Public TX simulator apps tests: AvmMinimalTestContract', () => {
  const deployer = AztecAddress.fromNumber(42);

  let avmMinimalTestContract: ContractInstanceWithAddress;
  let simTester: PublicTxSimulationTester;

  beforeEach(async () => {
    simTester = await PublicTxSimulationTester.create();

    avmMinimalTestContract = await simTester.registerAndDeployContract(
      /*constructorArgs=*/ [],
      deployer,
      /*contractArtifact=*/ AvmMinimalTestContractArtifact,
    );
  });

  it('minimal testing', async () => {
    const args = [1, 2].map(x => new Fr(x));

    const result = await simTester.simulateTx(
      /*sender=*/ deployer,
      /*setupCalls=*/ [],
      /*appCalls=*/ [
        {
          address: avmMinimalTestContract.address,
          fnName: 'add_u8',
          args,
        },
      ],
    );

    expect(result.revertCode.isOK()).toBe(true);
  });
});
