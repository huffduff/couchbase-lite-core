//
// DatabaseImpl.hh
//
// Copyright (c) 2016 Couchbase, Inc All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once

#include "c4Database.hh"
#include "c4DocumentTypes.h"
#include "DataFile.hh"
#include "FilePath.hh"
#include "InstanceCounted.hh"
#include "access_lock.hh"
#include <mutex>
#include <unordered_set>

C4_ASSUME_NONNULL_BEGIN

namespace fleece { namespace impl {
    class Dict;
    class Encoder;
    class SharedKeys;
    class Value;
} }

namespace litecore {
    class BackgroundDB;
    class BlobStore;
    class DocumentFactory;
    class Housekeeper;
    class RevTreeRecord;
    class SequenceTracker;


    /** The implementation of the C4Database class. */
    class DatabaseImpl final : public C4Database,
                               public DataFile::Delegate
    {
    public:
        DatabaseImpl(const string &path, C4DatabaseConfig config);

        void close();
        void deleteDatabase();
        static bool deleteDatabaseAtPath(const string &dbPath);

        DataFile* dataFile()                            {return _dataFile.get();}
        const DataFile* dataFile() const                {return _dataFile.get();}
        const string& name() const                      {return _name;}
        FilePath path() const;

        uint64_t countDocuments();
        sequence_t lastSequence()                       {return defaultKeyStore().lastSequence();}

        uint32_t maxRevTreeDepth();
        void setMaxRevTreeDepth(uint32_t depth);

        static const slice kPublicUUIDKey;
        static const slice kPrivateUUIDKey;

        C4UUID getUUID(slice key);
        void resetUUIDs();

        uint64_t myPeerID() const;

        void rekey(const C4EncryptionKey* C4NULLABLE newKey);
        
        void maintenance(DataFile::MaintenanceType what);

        const C4DatabaseConfig2* config() const         {return &_config;}
        const C4DatabaseConfig* configV1() const        {return &_configV1;};   // TODO: DEPRECATED

        ExclusiveTransaction& transaction() const;

        void beginTransaction();
        void endTransaction(bool commit);

        bool inTransaction() noexcept;
        void mustBeInTransaction();
        bool mustBeInTransaction(C4Error* C4NULLABLE outError) noexcept;
        bool mustNotBeInTransaction(C4Error* C4NULLABLE outError) noexcept;

        KeyStore& defaultKeyStore() const;
        KeyStore& getKeyStore(const string &name) const;

        bool purgeDocument(slice docID);
        int64_t purgeExpiredDocs();
        bool setExpiration(slice docID, expiration_t);
        bool startHousekeeping();

        void validateRevisionBody(slice body);

        Record getRawRecord(const std::string &storeName, slice key);
        void putRawRecord(const string &storeName, slice key, slice meta, slice body);

        DocumentFactory& documentFactory() const                {return *_documentFactory;}

        Retained<C4Document> getDocument(slice docID,
                                           bool mustExist,
                                           C4DocContentLevel content) const;
        Retained<C4Document> putDocument(const C4DocPutRequest &rq,
                                         size_t* C4NULLABLE outCommonAncestorIndex,
                                         C4Error* C4NULLABLE outError);

        static C4Document* documentContainingValue(FLValue) noexcept;

        fleece::impl::Encoder& sharedEncoder() const;
        FLEncoder sharedFLEncoder() const;

        fleece::impl::SharedKeys* documentKeys() const          {return _dataFile->documentKeys();}

        access_lock<SequenceTracker>& sequenceTracker();

        C4BlobStore& getBlobStore() const;

        void lockClientMutex() noexcept                         {_clientMutex.lock();}
        void unlockClientMutex() noexcept                       {_clientMutex.unlock();}

        // DataFile::Delegate API:
        virtual alloc_slice blobAccessor(const fleece::impl::Dict*) const override;
        virtual void externalTransactionCommitted(const SequenceTracker&) override;

        BackgroundDB* backgroundDatabase();
        void stopBackgroundTasks();

    public:
        // should be private, but called from Document
        void documentSaved(C4Document*);

    protected:
        virtual ~DatabaseImpl();
        void mustNotBeInTransaction();

    private:
        DatabaseImpl(const string &bundlePath, const C4DatabaseConfig&, FilePath &&dataFilePath);
        static FilePath findOrCreateBundle(const string &path, bool canCreate,
                                           const char * C4NONNULL &outStorageEngine);
        static bool deleteDatabaseFileAtPath(const string &dbPath, C4StorageEngine);
        void _cleanupTransaction(bool committed);
        bool getUUIDIfExists(slice key, C4UUID&);
        C4UUID generateUUID(slice key, bool overwrite =false);

        unique_ptr<BlobStore> createBlobStore(const std::string &dirname, C4EncryptionKey) const;
        std::unordered_set<std::string> collectBlobs();
        void removeUnusedBlobs(const std::unordered_set<std::string> &used);

        C4DocumentVersioning checkDocumentVersioning();
        void upgradeDocumentVersioning(C4DocumentVersioning old, C4DocumentVersioning nuu,
                                       ExclusiveTransaction&);
        alloc_slice upgradeRemoteRevsToVersionVectors(RevTreeRecord&, alloc_slice currentVersion);

        bool isNewDocPutRequest(const C4DocPutRequest &rq);
        std::pair<Retained<C4Document>,int> putNewDoc(const C4DocPutRequest &rq);

        const string                _name;                  // Database filename (w/o extension)
        const string                _parentDirectory;       // Path to parent directory
        C4DatabaseConfig2           _config;                // Configuration
        C4DatabaseConfig            _configV1;              // TODO: DEPRECATED
        unique_ptr<DataFile>        _dataFile;              // Underlying DataFile
        ExclusiveTransaction* C4NULLABLE _transaction {nullptr}; // Current ExclusiveTransaction, or null
        int                         _transactionLevel {0};  // Nesting level of transactions
        unique_ptr<DocumentFactory> _documentFactory;       // Instantiates C4Documents
        mutable unique_ptr<fleece::impl::Encoder> _encoder; // Shared Fleece Encoder
        mutable FLEncoder C4NULLABLE _flEncoder {nullptr};  // Ditto, for clients
        unique_ptr<access_lock<SequenceTracker>> _sequenceTracker; // Doc change tracker/notifier
        mutable unique_ptr<C4BlobStore> _blobStore;         // Blob storage
        uint32_t                    _maxRevTreeDepth {0};   // Max revision-tree depth
        std::recursive_mutex        _clientMutex;           // Mutex for c4db_lock/unlock
        unique_ptr<BackgroundDB>    _backgroundDB;          // for background operations
        Retained<Housekeeper>       _housekeeper;           // for expiration/cleanup tasks
        mutable uint64_t            _myPeerID {0};          // My identifier in version vectors
    };

}

C4_ASSUME_NONNULL_END