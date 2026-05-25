# Regras iniciais (Firestore e Storage)

## Firestore rules (inicial)
```txt
rules_version = '2';
service cloud.firestore {
  match /databases/{database}/documents {
    match /devices/{deviceId}/{document=**} {
      allow read: if request.auth != null;
      allow write: if false; // somente Admin SDK (bridge)
    }

    match /commands/{docId} {
      allow read, write: if request.auth != null;
    }
  }
}
```

## Storage rules (inicial)
```txt
rules_version = '2';
service firebase.storage {
  match /b/{bucket}/o {
    match /devices/{deviceId}/images/{allPaths=**} {
      allow read: if request.auth != null;
      allow write: if false; // somente backend autenticado (Admin SDK)
    }
  }
}
```

## Observação
Com Admin SDK no bridge, gravações no Firestore/Storage não dependem dessas regras de cliente.
