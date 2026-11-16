// Test du client BACnet.js (@bacnet-js/client) contre le serveur SmartCampus.
// Vérifie les 4 services demandés par le client : ReadProperty,
// WriteProperty, CreateObject et DeleteObject.
//
// Usage : node test.mjs <ip-du-serveur> [device] [port-local]
//   node test.mjs 10.42.0.1 1234
// Le port local (47809 par défaut) permet de lancer le test sur la même
// machine que le serveur, qui occupe déjà le port BACnet 47808.
// Le paquet est publié en CommonJS : la classe du client est dans .default
import bacnet from '@bacnet-js/client';

const Bacnet = bacnet.default;
const {
  ApplicationTag,
  ASN1_ARRAY_ALL,
  ASN1_NO_PRIORITY,
  EngineeringUnits,
  ObjectType,
  PropertyIdentifier,
} = bacnet;

// Valeur initiale pour CreateObject. Contournement d'un bug de la v3.3.2 :
// sans "index" et "priority" explicites, la bibliothèque encode un index de
// tableau 0 et une priorité 0, et le serveur refuse la création
// (error device / configuration-in-progress).
function initialValue(propertyId, type, value) {
  return {
    property: { id: propertyId, index: ASN1_ARRAY_ALL },
    value: [{ type, value }],
    priority: ASN1_NO_PRIORITY,
  };
}

const [serverIp = '127.0.0.1', deviceArg = '1234', portArg = '47809'] =
  process.argv.slice(2);
const device = { type: ObjectType.DEVICE, instance: Number(deviceArg) };
const server = { address: `${serverIp}:47808` };
const testObject = { type: ObjectType.ANALOG_VALUE, instance: 100 };

const client = new Bacnet({ port: Number(portArg), apduTimeout: 3000 });
let failures = 0;

async function step(name, action) {
  try {
    const result = await action();
    console.log(`OK   ${name}${result === undefined ? '' : ` -> ${result}`}`);
  } catch (error) {
    failures++;
    console.log(`FAIL ${name} -> ${error.message}`);
  }
}

async function read(objectId, property) {
  const result = await client.readProperty(server, objectId, property);
  return result.values.map((v) => v.value).join(', ');
}

await step('ReadProperty device object-name', () =>
  read(device, PropertyIdentifier.OBJECT_NAME));

await step('CreateObject analog-value 100 (nom + unité)', () =>
  client.createObject(server, testObject, [
    initialValue(
      PropertyIdentifier.OBJECT_NAME,
      ApplicationTag.CHARACTER_STRING,
      'Test-BACnetJS',
    ),
    initialValue(
      PropertyIdentifier.UNITS,
      ApplicationTag.ENUMERATED,
      EngineeringUnits.DEGREES_CELSIUS,
    ),
  ]));

await step('ReadProperty analog-value 100 object-name', () =>
  read(testObject, PropertyIdentifier.OBJECT_NAME));

await step('WriteProperty analog-value 100 present-value = 21.5', () =>
  client.writeProperty(
    server,
    testObject,
    PropertyIdentifier.PRESENT_VALUE,
    [{ type: ApplicationTag.REAL, value: 21.5 }],
    { priority: 16 },
  ));

await step('ReadProperty analog-value 100 present-value', () =>
  read(testObject, PropertyIdentifier.PRESENT_VALUE));

await step('DeleteObject analog-value 100', () =>
  client.deleteObject(server, testObject));

await step('ReadProperty après suppression (doit échouer)', async () => {
  try {
    await read(testObject, PropertyIdentifier.OBJECT_NAME);
  } catch (error) {
    return `refusé comme attendu (${error.message})`;
  }
  throw new Error("l'objet existe encore");
});

client.close();
console.log(failures === 0 ? '\nTous les tests sont OK' : `\n${failures} échec(s)`);
process.exitCode = failures === 0 ? 0 : 1;
