# Regras iniciais (Firestore e Storage)

## Firestore (cliente Flutter)
```txt
rules_version = '2';
service cloud.firestore {
  match /databases/{database}/documents {
    match /devices/{deviceId} {
      allow read: if request.auth != null;
      allow write: if false;

      match /commands/{commandId} {
        allow create: if request.auth != null;
        allow read: if request.auth != null;
        allow update, delete: if false;
      }

      match /{subCollection=**}/{docId} {
        allow read: if request.auth != null;
        allow write: if false;
      }
    }
  }
}
```

## Storage (inicial)
```txt
rules_version = '2';
service firebase.storage {
  match /b/{bucket}/o {
    match /devices/{deviceId}/{allPaths=**} {
      allow read: if request.auth != null;
      allow write: if false;
    }
  }
}
```

> Observação: gravações de backend (Cloud Functions com Admin SDK) ignoram rules de cliente.
