/**
 *  Copyright 2014-2016 CyberVision, Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifdef KAA_USE_SQLITE_LOG_STORAGE

#include <kaa/log/SQLiteDBLogStorage.hpp>

#include <kaa/logging/Log.hpp>
#include <kaa/log/LogRecord.hpp>
#include <kaa/common/exception/KaaException.hpp>
#include <kaa/KaaClientProperties.hpp>
#include "kaa/IKaaClientContext.hpp"



#define KAA_LOGS_TABLE_NAME       "KAA_LOGS"
#define KAA_BUCKETS_TABLE_NAME    "KAA_BUCKETS"

#define KAA_LOGS_RECORD_ID_FIELD_NAME    "RECORD_ID"
#define KAA_LOGS_DATA_FIELD_NAME         "LOG_DATA"

#define KAA_BUCKETS_INNER_BUCKET_ID_FIELD_NAME    "IN_BUCKET_ID"
#define KAA_BUCKETS_OUTER_BUCKET_ID_FIELD_NAME    "OUT_BUCKET_ID"
#define KAA_BUCKETS_SIZE_IN_RECORDS_FIELD_NAME    "SIZE_IN_RECORDS"
#define KAA_BUCKETS_SIZE_IN_BYTES_FIELD_NAME      "SIZE_IN_BYTES"
#define KAA_BUCKETS_STATE_FIELD_NAME              "STATE" /* 0 - free, 1 - in use*/

#define KAA_BUCKET_ID_INDEX_NAME "KAA_BUCKET_ID_INDEX"

#define KAA_CREATE_LOGS_TABLE \
         "CREATE TABLE IF NOT EXISTS " KAA_LOGS_TABLE_NAME " ("  \
         KAA_LOGS_RECORD_ID_FIELD_NAME"                  INTEGER    PRIMARY KEY    AUTOINCREMENT," \
         KAA_BUCKETS_OUTER_BUCKET_ID_FIELD_NAME"         INTEGER," \
         KAA_LOGS_DATA_FIELD_NAME"                       BLOB);"

#define KAA_DROP_LOGS_TABLE \
        "DROP TABLE " KAA_LOGS_TABLE_NAME ";"

#define KAA_CREATE_BUCKETS_TABLE \
         "CREATE TABLE IF NOT EXISTS " KAA_BUCKETS_TABLE_NAME " ("  \
         KAA_BUCKETS_INNER_BUCKET_ID_FIELD_NAME "    INTEGER    PRIMARY KEY    AUTOINCREMENT," \
         KAA_BUCKETS_OUTER_BUCKET_ID_FIELD_NAME "    INTEGER    NOT NULL," \
         KAA_BUCKETS_SIZE_IN_RECORDS_FIELD_NAME "    INTEGER    DEFAULT 0," \
         KAA_BUCKETS_SIZE_IN_BYTES_FIELD_NAME "      INTEGER    DEFAULT 0," \
         KAA_BUCKETS_STATE_FIELD_NAME "              INTEGER    DEFAULT 0);"

#define KAA_DROP_BUCKETS_TABLE \
        "DROP TABLE " KAA_BUCKETS_TABLE_NAME ";"

#define KAA_CREATE_BUCKET_ID_INDEX \
    "CREATE INDEX IF NOT EXISTS " KAA_BUCKET_ID_INDEX_NAME " " \
    "ON " KAA_BUCKETS_TABLE_NAME " (" KAA_BUCKETS_OUTER_BUCKET_ID_FIELD_NAME ");"

#define KAA_INSERT_NEW_BUCKET \
    "INSERT INTO " KAA_BUCKETS_TABLE_NAME " " \
    " (" KAA_BUCKETS_OUTER_BUCKET_ID_FIELD_NAME ")" \
    "VALUES (?);"

#define KAA_INSERT_NEW_RECORD_IN_BUCKET \
    "INSERT INTO " KAA_LOGS_TABLE_NAME " " \
    " (" KAA_BUCKETS_OUTER_BUCKET_ID_FIELD_NAME "," KAA_LOGS_DATA_FIELD_NAME ")" \
    "VALUES (?,?);"


#define KAA_SELECT_BUCKET_RECORDS \
    "SELECT " KAA_LOGS_DATA_FIELD_NAME " "\
    "FROM " KAA_LOGS_TABLE_NAME " "\
    "WHERE " KAA_BUCKETS_OUTER_BUCKET_ID_FIELD_NAME " = ? " \
    "ORDER BY " KAA_LOGS_RECORD_ID_FIELD_NAME " ASC;"

#define KAA_MARK_ALL_BUCKETS_AS_FREE \
    "UPDATE " KAA_BUCKETS_TABLE_NAME " " \
    "SET " KAA_BUCKETS_STATE_FIELD_NAME " = 0;"

#define KAA_DELETE_BUCKET \
    "DELETE FROM " KAA_BUCKETS_TABLE_NAME " " \
    "WHERE " KAA_BUCKETS_OUTER_BUCKET_ID_FIELD_NAME " = ?;"

#define KAA_DELETE_BUCKET_RECORDS \
    "DELETE FROM " KAA_LOGS_TABLE_NAME " " \
    "WHERE " KAA_BUCKETS_OUTER_BUCKET_ID_FIELD_NAME " = ?;"

#define KAA_COUNT_RECORDS \
    "SELECT SUM(" KAA_BUCKETS_SIZE_IN_RECORDS_FIELD_NAME "), SUM(" KAA_BUCKETS_SIZE_IN_BYTES_FIELD_NAME ")" \
    "FROM " KAA_BUCKETS_TABLE_NAME ";"

#define KAA_GET_MAX_RECORD_COUNT_AND_BUCKET_SIZE \
    "SELECT MAX(" KAA_BUCKETS_SIZE_IN_RECORDS_FIELD_NAME "), MAX(" KAA_BUCKETS_SIZE_IN_BYTES_FIELD_NAME ")" \
    "FROM " KAA_BUCKETS_TABLE_NAME ";"

#define KAA_GET_THE_OLDEST_BUCKET \
    "SELECT * FROM " KAA_BUCKETS_TABLE_NAME " " \
    "WHERE " KAA_BUCKETS_STATE_FIELD_NAME " = 0 " \
    "ORDER BY " KAA_BUCKETS_INNER_BUCKET_ID_FIELD_NAME " ASC " \
    "LIMIT 1;"

#define KAA_MARK_BUCKET_AS_IN_USE \
    "UPDATE " KAA_BUCKETS_TABLE_NAME " " \
    "SET " KAA_BUCKETS_STATE_FIELD_NAME " = 1 " \
    "WHERE " KAA_BUCKETS_OUTER_BUCKET_ID_FIELD_NAME " = ?;"

#define KAA_MARK_BUCKET_AS_FREE \
    "UPDATE " KAA_BUCKETS_TABLE_NAME " " \
    "SET " KAA_BUCKETS_STATE_FIELD_NAME " = 0 " \
    "WHERE " KAA_BUCKETS_OUTER_BUCKET_ID_FIELD_NAME " = ?;"

#define KAA_UPDATE_BUCKET_INFO \
    "UPDATE " KAA_BUCKETS_TABLE_NAME " " \
    "SET " KAA_BUCKETS_SIZE_IN_RECORDS_FIELD_NAME " = " KAA_BUCKETS_SIZE_IN_RECORDS_FIELD_NAME "+ 1, " \
     KAA_BUCKETS_SIZE_IN_BYTES_FIELD_NAME " = " KAA_BUCKETS_SIZE_IN_BYTES_FIELD_NAME "+ ? " \
    "WHERE " KAA_BUCKETS_OUTER_BUCKET_ID_FIELD_NAME " = ?;"

/*
 * OPTIMIZATION OPTIONS.
 */
#define KAA_SYNCHRONIZATION_OPTION        "PRAGMA synchronous=OFF"
#define KAA_COUNT_CHANGES_OPTION          "PRAGMA count_changes=OFF"
#define KAA_MEMORY_JOURNAL_MODE_OPTION    "PRAGMA journal_mode=MEMORY"
#define KAA_MEMORY_TEMP_STORE_OPTION      "PRAGMA temp_store=MEMORY"

namespace kaa {

static void throwIfError(int errorCode, int expectedErrorCode, const std::string& errorMessage)
{
    if (errorCode != expectedErrorCode) {
        throw KaaException(errorMessage);
    }
}

class SQLiteStatement {
public:
    SQLiteStatement(sqlite3 *db, const char* sql)
    {
        if (!db || !sql) {
            throw KaaException("Failed to create sqlite3 statement: bad data");
        }

        int errorCode = sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr);
        throwIfError(errorCode, SQLITE_OK, (boost::format("Failed to create sql statement '%s' (error %d)") % sql % errorCode).str());
    }

    ~SQLiteStatement()
    {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
    }

    sqlite3_stmt *getStatement() { return stmt_; }

private:
    sqlite3_stmt *stmt_ = nullptr;
};

SQLiteDBLogStorage::SQLiteDBLogStorage(IKaaClientContext &context, std::size_t bucketSize, std::size_t bucketRecordCount)
    : dbName_(context.getProperties().getLogsDatabaseFileName()),
      maxBucketSize_(bucketSize), maxBucketRecordCount_(bucketRecordCount),
      context_(context)
{
    init(SQLiteOptimizationOptions::SQLITE_NO_OPTIMIZATIONS);
}

SQLiteDBLogStorage::SQLiteDBLogStorage(IKaaClientContext &context,
                                       const std::string& dbName, int optimizationMask,
                                       std::size_t bucketSize, std::size_t bucketRecordCount)
    : dbName_(dbName), maxBucketSize_(bucketSize), maxBucketRecordCount_(bucketRecordCount), context_(context)
{
    init(optimizationMask);
}

SQLiteDBLogStorage::~SQLiteDBLogStorage()
{
    closeDBConnection();
}

void SQLiteDBLogStorage::init(int optimizationMask)
{
    openDBConnection();
    applyOptimization(optimizationMask);

    initDBTables();

    if (truncateIfBucketSizeIncompatible()) {
        initDBTables();
    } else {
        SQLiteStatement stmt(db_, KAA_COUNT_RECORDS);
        int errorCode = sqlite3_step(stmt.getStatement());
        if (errorCode == SQLITE_ROW) {
            currentBucketRecordCount_ = totalRecordCount_ = unmarkedRecordCount_ = sqlite3_column_int64(stmt.getStatement(), 0);
            currentBucketSize_ = consumedMemory_ = sqlite3_column_int64(stmt.getStatement(), 1);
        } else {
            KAA_LOG_ERROR("Failed to count log records in database");
        }
    }

    if (totalRecordCount_ > 0) {
        markBucketsAsFree();
    } else {
        addNextBucket();
    }

    KAA_LOG_INFO(boost::format("%li log records in database (%li bytes total size)") % totalRecordCount_ % consumedMemory_);
}

bool SQLiteDBLogStorage::truncateIfBucketSizeIncompatible()
{
    SQLiteStatement getMaxStmt(db_, KAA_GET_MAX_RECORD_COUNT_AND_BUCKET_SIZE);
    int errorCode = sqlite3_step(getMaxStmt.getStatement());
    if (errorCode == SQLITE_ROW) {
        std::size_t maxBucketSizeInRecordCount = sqlite3_column_int64(getMaxStmt.getStatement(), 0);
        std::size_t maxBucketSizeInBytes = sqlite3_column_int64(getMaxStmt.getStatement(), 1);

        if (maxBucketSizeInRecordCount > maxBucketRecordCount_ || maxBucketSizeInBytes > maxBucketSize_) {
            KAA_LOG_INFO(boost::format("Truncating logs: current_max_bucket_size %1% bytes, current_max_bucket_records %2%, "
                                                             "db_max_bucket_size %3% bytes, db_max_bucket_records %4%")
                                                                % maxBucketSize_ % maxBucketRecordCount_
                                                                % maxBucketSizeInBytes % maxBucketSizeInRecordCount);

            SQLiteStatement dropBucketTableStmt(db_, KAA_DROP_BUCKETS_TABLE);
            sqlite3_step(dropBucketTableStmt.getStatement());

            SQLiteStatement dropLogsTableStmt(db_, KAA_DROP_LOGS_TABLE);
            sqlite3_step(dropLogsTableStmt.getStatement());

            return true;
        }
    }

    return false;
}

void SQLiteDBLogStorage::initDBTables()
{
    try {
        SQLiteStatement createBucketsTableStmt(db_, KAA_CREATE_BUCKETS_TABLE);
        int errorCode = sqlite3_step(createBucketsTableStmt.getStatement());
        throwIfError(errorCode, SQLITE_DONE,
                (boost::format("Failed to create '" KAA_BUCKETS_TABLE_NAME "' table (error %d)") % errorCode).str());

        KAA_LOG_TRACE("'" KAA_BUCKETS_TABLE_NAME "' table created");

        SQLiteStatement createLogsTableStmt(db_, KAA_CREATE_LOGS_TABLE);
        errorCode = sqlite3_step(createLogsTableStmt.getStatement());
        throwIfError(errorCode, SQLITE_DONE,
                (boost::format("Failed to create '" KAA_LOGS_TABLE_NAME "' table (error %d)") % errorCode).str());

        KAA_LOG_TRACE("'" KAA_LOGS_TABLE_NAME "' table created");

        //TODO: to increase performance need to create some indexes on DB tables.
    } catch (std::exception& e) {
        KAA_LOG_FATAL(boost::format("Failed to init log table: %s") % e.what());
        throw;
    }
}

void SQLiteDBLogStorage::applyOptimization(int mask)
{
    if (mask == SQLiteOptimizationOptions::SQLITE_NO_OPTIMIZATIONS) {
        KAA_LOG_INFO("No database optimization option is used");
        return;
    }

    if (mask & SQLiteOptimizationOptions::SQLITE_SYNCHRONOUS_OFF) {
        sqlite3_exec(db_, KAA_SYNCHRONIZATION_OPTION, nullptr, nullptr, nullptr);
        KAA_LOG_INFO(boost::format("Applied '%s' optimization") % KAA_SYNCHRONIZATION_OPTION);
    }
    if (mask & SQLiteOptimizationOptions::SQLITE_MEMORY_JOURNAL_MODE) {
        sqlite3_exec(db_, KAA_MEMORY_JOURNAL_MODE_OPTION, nullptr, nullptr, nullptr);
        KAA_LOG_INFO(boost::format("Applied '%s' optimization") % KAA_MEMORY_JOURNAL_MODE_OPTION);
    }
    if (mask & SQLiteOptimizationOptions::SQLITE_MEMORY_TEMP_STORE) {
        sqlite3_exec(db_, KAA_MEMORY_TEMP_STORE_OPTION, nullptr, nullptr, nullptr);
        KAA_LOG_INFO(boost::format("Applied '%s' optimization") % KAA_MEMORY_TEMP_STORE_OPTION);
    }
    if (mask & SQLiteOptimizationOptions::SQLITE_COUNT_CHANGES_OFF) {
        sqlite3_exec(db_, KAA_COUNT_CHANGES_OPTION, nullptr, nullptr, nullptr);
        KAA_LOG_INFO(boost::format("Applied '%s' optimization") % KAA_COUNT_CHANGES_OPTION);
    }
}

void SQLiteDBLogStorage::markBucketsAsFree()
{
    try {
        SQLiteStatement stmt(db_, KAA_MARK_ALL_BUCKETS_AS_FREE);
        int errorCode = sqlite3_step(stmt.getStatement());
        throwIfError(errorCode, SQLITE_DONE, (boost::format("(error %d)") % errorCode).str());

        KAA_LOG_INFO(boost::format("Mark %1% bucket(s) as free") % sqlite3_changes(db_));
    } catch (std::exception& e) {
        KAA_LOG_FATAL(boost::format("Failed to mark bucket(s) as free: %s") % e.what());
        throw;
    }
}

void SQLiteDBLogStorage::markBucketAsInUse(std::int32_t id)
{
    SQLiteStatement stmt(db_, KAA_MARK_BUCKET_AS_IN_USE);

    int errorCode = sqlite3_bind_int64(stmt.getStatement(), 1, id);
    throwIfError(errorCode, SQLITE_OK, (boost::format("Failed to bind bucket id (error %d)") % errorCode).str());

    errorCode = sqlite3_step(stmt.getStatement());
    throwIfError(errorCode, SQLITE_DONE, (boost::format("(error %d)") % errorCode).str());

    KAA_LOG_TRACE(boost::format("Mark bucket as in use, id %1%") % id);
}

void SQLiteDBLogStorage::openDBConnection()
{
    KAA_LOG_TRACE(boost::format("Going to connect to '%s' log database") % dbName_);

    int errorCode = sqlite3_open(dbName_.c_str(), &db_);
    throwIfError(errorCode, SQLITE_OK, (boost::format("Failed to connect to '%s' log database (error %d)") % dbName_ % errorCode).str());

    KAA_LOG_INFO(boost::format("Connected to '%s' log database") % dbName_);
}

void SQLiteDBLogStorage::closeDBConnection()
{
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

BucketInfo SQLiteDBLogStorage::addLogRecord(LogRecord&& record)
{
    auto recordSize = record.getSize();
    if (recordSize > maxBucketSize_) {
        KAA_LOG_WARN(boost::format("Failed to add log record: record_size %1%B, max_bucket_size %2%B")
                                                                        % recordSize % maxBucketSize_);
        throw KaaException("Too big log record");
    }

    if (checkBucketOverflow(record)) {
        addNextBucket();
    }

    SQLiteStatement insertStmt(db_, KAA_INSERT_NEW_RECORD_IN_BUCKET);

    int errorCode = sqlite3_bind_int(insertStmt.getStatement(), 1, currentBucketId_);
    throwIfError(errorCode, SQLITE_OK, (boost::format("Failed to bind record's bucket id (error %d)") % errorCode).str());

    errorCode = sqlite3_bind_blob(insertStmt.getStatement(), 2, record.getData().data(), record.getSize(), SQLITE_STATIC);
    throwIfError(errorCode, SQLITE_OK, (boost::format("Failed to bind record data (error %d)") % errorCode).str());

    SQLiteStatement updateBucketInfoStmt(db_, KAA_UPDATE_BUCKET_INFO);

    errorCode = sqlite3_bind_int(updateBucketInfoStmt.getStatement(), 1, record.getSize());
    throwIfError(errorCode, SQLITE_OK, (boost::format("Failed to bind record size (error %d)") % errorCode).str());

    errorCode = sqlite3_bind_int(updateBucketInfoStmt.getStatement(), 2, currentBucketId_);
    throwIfError(errorCode, SQLITE_OK, (boost::format("Failed to bind bucket id (error %d)") % errorCode).str());

    KAA_MUTEX_LOCKING("sqliteLogStorageGuard_");
    KAA_MUTEX_UNIQUE_DECLARE(storageGuardLock, sqliteLogStorageGuard_);
    KAA_MUTEX_LOCKED("sqliteLogStorageGuard_");

    errorCode = sqlite3_step(insertStmt.getStatement());
    throwIfError(errorCode, SQLITE_DONE, (boost::format("Failed to execute sql insert query (error %d)") % errorCode).str());

    errorCode = sqlite3_step(updateBucketInfoStmt.getStatement());
    throwIfError(errorCode, SQLITE_DONE, (boost::format("Failed to execute sql insert query (error %d)") % errorCode).str());

    ++unmarkedRecordCount_;
    ++totalRecordCount_;
    consumedMemory_ += record.getSize();

    ++currentBucketRecordCount_;
    currentBucketSize_ += record.getSize();

    KAA_LOG_TRACE(boost::format("Added log record (%u bytes). Total: %u, unmarked: %u, consumedMemory: %u")
                            % record.getSize() % totalRecordCount_ % unmarkedRecordCount_ % consumedMemory_);

    return BucketInfo(currentBucketId_, currentBucketRecordCount_);
}

LogBucket SQLiteDBLogStorage::getNextBucket()
{
    try {
        SQLiteStatement getOldestBucketStmt(db_, KAA_GET_THE_OLDEST_BUCKET);

        KAA_MUTEX_LOCKING("sqliteLogStorageGuard_");
        KAA_MUTEX_UNIQUE_DECLARE(storageGuardLock, sqliteLogStorageGuard_);
        KAA_MUTEX_LOCKED("sqliteLogStorageGuard_");

        int errorCode = sqlite3_step(getOldestBucketStmt.getStatement());
        if (errorCode != SQLITE_ROW) {
            KAA_LOG_DEBUG(boost::format("Failed to find unused bucket, error %1%") % errorCode);
        }

        std::int32_t bucketId = sqlite3_column_int64(getOldestBucketStmt.getStatement(), 1);
        std::size_t bucketSizeInRecords = sqlite3_column_int64(getOldestBucketStmt.getStatement(), 2);
        std::size_t bucketSizeInBytes = sqlite3_column_int64(getOldestBucketStmt.getStatement(), 3);

        SQLiteStatement getBucketLogsStmt(db_, KAA_SELECT_BUCKET_RECORDS);

        errorCode = sqlite3_bind_int(getBucketLogsStmt.getStatement(), 1, bucketId);
        throwIfError(errorCode, SQLITE_OK, (boost::format("Failed to bind record data (error %d)") % errorCode).str());

        std::list<LogRecord> records;

        while (SQLITE_ROW == (errorCode = sqlite3_step(getBucketLogsStmt.getStatement()))) {
            const void *recordData = sqlite3_column_blob(getBucketLogsStmt.getStatement(), 0);
            int recordDataSize = sqlite3_column_bytes(getBucketLogsStmt.getStatement(), 0);
            records.emplace_back(reinterpret_cast<const std::uint8_t *>(recordData), recordDataSize);
        }

        throwIfError(errorCode, SQLITE_DONE, (boost::format("Failed to execute sql select query (error %d)") % errorCode).str());

        markBucketAsInUse(bucketId);

        unmarkedRecordCount_ -= bucketSizeInRecords;
        consumedMemory_ -= bucketSizeInBytes;
        consumedMemoryStorage_.insert(std::make_pair(bucketId, InnerBucketInfo(bucketSizeInBytes, bucketSizeInRecords)));

        KAA_LOG_INFO(boost::format("Create log bucket: id %d, size %u, %u record(s). Total: %u, unmarked: %u, consumedMemory: %u")
                        % bucketId % bucketSizeInBytes % bucketSizeInRecords % totalRecordCount_ % unmarkedRecordCount_ % consumedMemory_);

        if (unmarkedRecordCount_) {
            addNextBucket();
        }

        return LogBucket(bucketId, std::move(records));
    } catch (std::exception& e) {
        KAA_LOG_ERROR(boost::format("Failed to create log bucket (total: %u, unmarked: %u, consumedMemory: %u): %s")
                                            % totalRecordCount_ % unmarkedRecordCount_ % consumedMemory_ % e.what());
    }

    return LogBucket();
}


void SQLiteDBLogStorage::removeBucket(std::int32_t bucketId)
{
    try {
        SQLiteStatement deleteBucketStmt(db_, KAA_DELETE_BUCKET);

        int errorCode = sqlite3_bind_int64(deleteBucketStmt.getStatement(), 1, bucketId);
        throwIfError(errorCode, SQLITE_OK, (boost::format("Failed to bind bucket id (error %d)") % errorCode).str());

        SQLiteStatement deleteBucketRecordsStmt(db_, KAA_DELETE_BUCKET_RECORDS);

        errorCode = sqlite3_bind_int64(deleteBucketRecordsStmt.getStatement(), 1, bucketId);
        throwIfError(errorCode, SQLITE_OK, (boost::format("Failed to bind bucket id (error %d)") % errorCode).str());

        KAA_MUTEX_LOCKING("sqliteLogStorageGuard_");
        KAA_MUTEX_UNIQUE_DECLARE(storageGuardLock, sqliteLogStorageGuard_);
        KAA_MUTEX_LOCKED("sqliteLogStorageGuard_");

        errorCode = sqlite3_step(deleteBucketStmt.getStatement());
        throwIfError(errorCode, SQLITE_DONE, (boost::format("Failed to execute sql delete query (error %d)") % errorCode).str());

        errorCode = sqlite3_step(deleteBucketRecordsStmt.getStatement());
        throwIfError(errorCode, SQLITE_DONE, (boost::format("Failed to execute sql delete query (error %d)") % errorCode).str());

        int removedRecordsCount = sqlite3_changes(db_);
        totalRecordCount_ -= removedRecordsCount;
        consumedMemoryStorage_.erase(bucketId);

        KAA_LOG_INFO(boost::format("Removed %d log records, bucket id %d. Total: %u, unmarked: %u, consumedMemory: %u")
                                % removedRecordsCount % bucketId % totalRecordCount_ % unmarkedRecordCount_ % consumedMemory_);
    } catch (std::exception& e) {
        KAA_LOG_ERROR(boost::format("Failed to remove log records by bucket id %d (total: %u, unmarked: %u, consumedMemory: %u): %s")
                                                % bucketId % totalRecordCount_ % unmarkedRecordCount_ % consumedMemory_ % e.what());
    }
}

void SQLiteDBLogStorage::rollbackBucket(std::int32_t bucketId)
{
    try {
        int errorCode = SQLITE_OK;
        SQLiteStatement stmt(db_, KAA_MARK_BUCKET_AS_FREE);

        errorCode = sqlite3_bind_int64(stmt.getStatement(), 1, bucketId);
        throwIfError(errorCode, SQLITE_OK, (boost::format("Failed to bind bucket id (error %d)") % errorCode).str());

        KAA_MUTEX_LOCKING("sqliteLogStorageGuard_");
        KAA_MUTEX_UNIQUE_DECLARE(storageGuardLock, sqliteLogStorageGuard_);
        KAA_MUTEX_LOCKED("sqliteLogStorageGuard_");

        errorCode = sqlite3_step(stmt.getStatement());
        throwIfError(errorCode, SQLITE_DONE, (boost::format("Failed to execute sql update query (error %d)") % errorCode).str());

        auto it = consumedMemoryStorage_.find(bucketId);
        if (it != consumedMemoryStorage_.end()) {
            consumedMemory_ += it->second.sizeInBytes_;
            unmarkedRecordCount_ += it->second.sizeInLogs_;
            consumedMemoryStorage_.erase(it);
        }

        KAA_LOG_INFO(boost::format("Rollback bucket (id %d). Total: %u, unmarked: %u, consumedMemory: %u")
                                    % bucketId % totalRecordCount_ % unmarkedRecordCount_ % consumedMemory_);
    } catch (std::exception& e) {
        KAA_LOG_ERROR(boost::format("Failed to rollback bucket (id %d): %s. Total: %u, unmarked: %u, consumedMemory: %u.")
                                            % bucketId % e.what() % totalRecordCount_ % unmarkedRecordCount_ % consumedMemory_);
    }
}


std::size_t SQLiteDBLogStorage::getRecordsCount()
{
    KAA_MUTEX_LOCKING("sqliteLogStorageGuard_");
    KAA_MUTEX_UNIQUE_DECLARE(storageGuardLock, sqliteLogStorageGuard_);
    KAA_MUTEX_LOCKED("sqliteLogStorageGuard_");
    return unmarkedRecordCount_;
}

std::size_t SQLiteDBLogStorage::getConsumedVolume()
{
    KAA_MUTEX_LOCKING("sqliteLogStorageGuard_");
    KAA_MUTEX_UNIQUE_DECLARE(storageGuardLock, sqliteLogStorageGuard_);
    KAA_MUTEX_LOCKED("sqliteLogStorageGuard_");
    return consumedMemory_;
}

void SQLiteDBLogStorage::addNextBucket()
{
    currentBucketId_++;
    currentBucketSize_ = currentBucketRecordCount_ = 0;

    SQLiteStatement insertStmt(db_, KAA_INSERT_NEW_BUCKET);

    int errorCode = sqlite3_bind_int(insertStmt.getStatement(), 1, currentBucketId_);
    throwIfError(errorCode, SQLITE_OK, (boost::format("Failed to bind record data (error %d)") % errorCode).str());

    errorCode = sqlite3_step(insertStmt.getStatement());
    throwIfError(errorCode, SQLITE_DONE, (boost::format("Failed to execute sql insert query (error %d)") % errorCode).str());

    KAA_LOG_INFO(boost::format("Add new bucket, id %1%") % currentBucketId_);
}

} /* namespace kaa */

#endif /* KAA_USE_SQLITE_LOG_STORAGE */
