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

      match /settings/{settingId} {
        allow read: if request.auth != null;
        allow create, update: if request.auth != null;
        allow delete: if false;
      }

      match /images/{imageId} {
        allow read: if request.auth != null;
        allow write: if false;
      }

      match /climate/{readingId} {
        allow read: if request.auth != null;
        allow write: if false;
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
    match /devices/{deviceId}/images/{imageName} {
      allow read: if request.auth != null;
      allow write: if false;
    }

    match /devices/{deviceId}/{allPaths=**} {
      allow read: if request.auth != null;
      allow write: if false;
    }
  }
}
```

> Observação: gravações de backend (Cloud Functions com Admin SDK) ignoram rules de cliente. O upload da câmera deve ocorrer pela Function `uploadCameraImage`, não diretamente pelo app ou pelo cliente anônimo. As leituras do módulo clima devem ser gravadas pela Function `ingestClimateReading`.
