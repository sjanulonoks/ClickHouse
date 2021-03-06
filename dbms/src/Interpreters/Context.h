#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include <Core/Types.h>
#include <Core/NamesAndTypes.h>
#include <Interpreters/Settings.h>
#include <Interpreters/ClientInfo.h>
#include <IO/CompressedStream.h>


namespace Poco
{
    namespace Net
    {
        class IPAddress;
    }
}

namespace zkutil
{
    class ZooKeeper;
}


namespace DB
{

struct ContextShared;
class QuotaForIntervals;
class EmbeddedDictionaries;
class ExternalDictionaries;
class InterserverIOHandler;
class BackgroundProcessingPool;
class ReshardingWorker;
class MergeList;
class Cluster;
class Compiler;
class MarkCache;
class UncompressedCache;
class ProcessList;
struct ProcessListElement;
class Macros;
struct Progress;
class Clusters;
class QueryLog;
class PartLog;
struct MergeTreeSettings;
class IDatabase;
class DDLGuard;
class DDLWorker;
class IStorage;
using StoragePtr = std::shared_ptr<IStorage>;
using Tables = std::map<String, StoragePtr>;
class IAST;
using ASTPtr = std::shared_ptr<IAST>;
class IBlockInputStream;
class IBlockOutputStream;
using BlockInputStreamPtr = std::shared_ptr<IBlockInputStream>;
using BlockOutputStreamPtr = std::shared_ptr<IBlockOutputStream>;
class Block;
struct SystemLogs;
using SystemLogsPtr = std::shared_ptr<SystemLogs>;


/// (database name, table name)
using DatabaseAndTableName = std::pair<String, String>;

/// Table -> set of table-views that make SELECT from it.
using ViewDependencies = std::map<DatabaseAndTableName, std::set<DatabaseAndTableName>>;
using Dependencies = std::vector<DatabaseAndTableName>;


/** A set of known objects that can be used in the query.
  * Consists of a shared part (always common to all sessions and queries)
  *  and copied part (which can be its own for each session or query).
  *
  * Everything is encapsulated for all sorts of checks and locks.
  */
class Context
{
private:
    using Shared = std::shared_ptr<ContextShared>;
    Shared shared;

    ClientInfo client_info;

    std::shared_ptr<QuotaForIntervals> quota;           /// Current quota. By default - empty quota, that have no limits.
    String current_database;
    Settings settings;                                  /// Setting for query execution.
    using ProgressCallback = std::function<void(const Progress & progress)>;
    ProgressCallback progress_callback;                 /// Callback for tracking progress of query execution.
    ProcessListElement * process_list_elem = nullptr;   /// For tracking total resource usage for query.

    String default_format;  /// Format, used when server formats data by itself and if query does not have FORMAT specification.
                            /// Thus, used in HTTP interface. If not specified - then some globally default format is used.
    Tables external_tables;                 /// Temporary tables.
    Context * session_context = nullptr;    /// Session context or nullptr. Could be equal to this.
    Context * global_context = nullptr;     /// Global context or nullptr. Could be equal to this.
    SystemLogsPtr system_logs;              /// Used to log queries and operations on parts

    UInt64 session_close_cycle = 0;
    bool session_is_used = false;

    using DatabasePtr = std::shared_ptr<IDatabase>;
    using Databases = std::map<String, std::shared_ptr<IDatabase>>;

    /// Use copy constructor or createGlobal() instead
    Context();

public:
    /// Create initial Context with ContextShared and etc.
    static Context createGlobal();

    ~Context();

    String getPath() const;
    String getTemporaryPath() const;
    String getFlagsPath() const;
    void setPath(const String & path);
    void setTemporaryPath(const String & path);
    void setFlagsPath(const String & path);

    using ConfigurationPtr = Poco::AutoPtr<Poco::Util::AbstractConfiguration>;

    /** Take the list of users, quotas and configuration profiles from this config.
      * The list of users is completely replaced.
      * The accumulated quota values are not reset if the quota is not deleted.
      */
    void setUsersConfig(const ConfigurationPtr & config);

    ConfigurationPtr getUsersConfig();

    /// Must be called before getClientInfo.
    void setUser(const String & name, const String & password, const Poco::Net::SocketAddress & address, const String & quota_key);
    /// Compute and set actual user settings, client_info.current_user should be set
    void calculateUserSettings();

    ClientInfo & getClientInfo() { return client_info; };
    const ClientInfo & getClientInfo() const { return client_info; };

    void setQuota(const String & name, const String & quota_key, const String & user_name, const Poco::Net::IPAddress & address);
    QuotaForIntervals & getQuota();

    void addDependency(const DatabaseAndTableName & from, const DatabaseAndTableName & where);
    void removeDependency(const DatabaseAndTableName & from, const DatabaseAndTableName & where);
    Dependencies getDependencies(const String & database_name, const String & table_name) const;

    /// Checking the existence of the table/database. Database can be empty - in this case the current database is used.
    bool isTableExist(const String & database_name, const String & table_name) const;
    bool isDatabaseExist(const String & database_name) const;
    void assertTableExists(const String & database_name, const String & table_name) const;

    /** The parameter check_database_access_rights exists to not check the permissions of the database again,
      * when assertTableDoesntExist or assertDatabaseExists is called inside another function that already
      * made this check.
      */
    void assertTableDoesntExist(const String & database_name, const String & table_name, bool check_database_acccess_rights = true) const;
    void assertDatabaseExists(const String & database_name, bool check_database_acccess_rights = true) const;

    void assertDatabaseDoesntExist(const String & database_name) const;

    Tables getExternalTables() const;
    StoragePtr tryGetExternalTable(const String & table_name) const;
    StoragePtr getTable(const String & database_name, const String & table_name) const;
    StoragePtr tryGetTable(const String & database_name, const String & table_name) const;
    void addExternalTable(const String & table_name, StoragePtr storage);

    void addDatabase(const String & database_name, const DatabasePtr & database);
    DatabasePtr detachDatabase(const String & database_name);

    /// Get an object that protects the table from concurrently executing multiple DDL operations.
    /// If such an object already exists, an exception is thrown.
    std::unique_ptr<DDLGuard> getDDLGuard(const String & database, const String & table, const String & message) const;
    /// If the table already exists, it returns nullptr, otherwise guard is created.
    std::unique_ptr<DDLGuard> getDDLGuardIfTableDoesntExist(const String & database, const String & table, const String & message) const;

    String getCurrentDatabase() const;
    String getCurrentQueryId() const;
    void setCurrentDatabase(const String & name);
    void setCurrentQueryId(const String & query_id);

    String getDefaultFormat() const;    /// If default_format is not specified, some global default format is returned.
    void setDefaultFormat(const String & name);

    const Macros & getMacros() const;
    void setMacros(Macros && macros);

    Settings getSettings() const;
    void setSettings(const Settings & settings_);

    Limits getLimits() const;

    /// Set a setting by name.
    void setSetting(const String & name, const Field & value);

    /// Set a setting by name. Read the value in text form from a string (for example, from a config, or from a URL parameter).
    void setSetting(const String & name, const std::string & value);

    const EmbeddedDictionaries & getEmbeddedDictionaries() const;
    const ExternalDictionaries & getExternalDictionaries() const;
    void tryCreateEmbeddedDictionaries() const;
    void tryCreateExternalDictionaries() const;

    /// I/O formats.
    BlockInputStreamPtr getInputFormat(const String & name, ReadBuffer & buf, const Block & sample, size_t max_block_size) const;
    BlockOutputStreamPtr getOutputFormat(const String & name, WriteBuffer & buf, const Block & sample) const;

    InterserverIOHandler & getInterserverIOHandler();

    /// How other servers can access this for downloading replicated data.
    void setInterserverIOAddress(const String & host, UInt16 port);
    std::pair<String, UInt16> getInterserverIOAddress() const;
    /// The port that the server listens for executing SQL queries.
    UInt16 getTCPPort() const;

    /// Get query for the CREATE table.
    ASTPtr getCreateQuery(const String & database_name, const String & table_name) const;

    const DatabasePtr getDatabase(const String & database_name) const;
    DatabasePtr getDatabase(const String & database_name);
    const DatabasePtr tryGetDatabase(const String & database_name) const;
    DatabasePtr tryGetDatabase(const String & database_name);

    const Databases getDatabases() const;
    Databases getDatabases();

    std::shared_ptr<Context> acquireSession(const String & session_id, std::chrono::steady_clock::duration timeout, bool session_check) const;
    void releaseSession(const String & session_id, std::chrono::steady_clock::duration timeout);

    /// Close sessions, that has been expired. Returns how long to wait for next session to be expired, if no new sessions will be added.
    std::chrono::steady_clock::duration closeSessions() const;

    /// For methods below you may need to acquire a lock by yourself.
    std::unique_lock<Poco::Mutex> getLock() const;

    const Context & getSessionContext() const;
    Context & getSessionContext();

    const Context & getGlobalContext() const;
    Context & getGlobalContext();

    void setSessionContext(Context & context_)                                { session_context = &context_; }
    void setGlobalContext(Context & context_)                                { global_context = &context_; }

    const Settings & getSettingsRef() const { return settings; };
    Settings & getSettingsRef() { return settings; };


    void setProgressCallback(ProgressCallback callback);
    /// Used in InterpreterSelectQuery to pass it to the IProfilingBlockInputStream.
    ProgressCallback getProgressCallback() const;

    /** Set in executeQuery and InterpreterSelectQuery. Then it is used in IProfilingBlockInputStream,
      *  to update and monitor information about the total number of resources spent for the query.
      */
    void setProcessListElement(ProcessListElement * elem);
    /// Can return nullptr if the query was not inserted into the ProcessList.
    ProcessListElement * getProcessListElement();

    /// List all queries.
    ProcessList & getProcessList();
    const ProcessList & getProcessList() const;

    MergeList & getMergeList();
    const MergeList & getMergeList() const;

    /// Create a cache of uncompressed blocks of specified size. This can be done only once.
    void setUncompressedCache(size_t max_size_in_bytes);
    std::shared_ptr<UncompressedCache> getUncompressedCache() const;

    void setZooKeeper(std::shared_ptr<zkutil::ZooKeeper> zookeeper);
    /// If the current session is expired at the time of the call, synchronously creates and returns a new session with the startNewSession() call.
    std::shared_ptr<zkutil::ZooKeeper> getZooKeeper() const;
    /// Has ready or expired ZooKeeper
    bool hasZooKeeper() const;

    /// Create a cache of marks of specified size. This can be done only once.
    void setMarkCache(size_t cache_size_in_bytes);
    std::shared_ptr<MarkCache> getMarkCache() const;

    BackgroundProcessingPool & getBackgroundPool();

    void setReshardingWorker(std::shared_ptr<ReshardingWorker> resharding_worker);
    ReshardingWorker & getReshardingWorker();

    void setDDLWorker(std::shared_ptr<DDLWorker> ddl_worker);
    DDLWorker & getDDLWorker();

    /** Clear the caches of the uncompressed blocks and marks.
      * This is usually done when renaming tables, changing the type of columns, deleting a table.
      *  - since caches are linked to file names, and become incorrect.
      *  (when deleting a table - it is necessary, since in its place another can appear)
      * const - because the change in the cache is not considered significant.
      */
    void resetCaches() const;

    Clusters & getClusters() const;
    std::shared_ptr<Cluster> getCluster(const std::string & cluster_name) const;
    std::shared_ptr<Cluster> tryGetCluster(const std::string & cluster_name) const;
    void setClustersConfig(const ConfigurationPtr & config);

    Compiler & getCompiler();
    QueryLog & getQueryLog();

    /// Returns an object used to log opertaions with parts if it possible.
    /// Provide table name to make required cheks.
    PartLog * getPartLog(const String & database, const String & table);
    const MergeTreeSettings & getMergeTreeSettings();

    /// Prevents DROP TABLE if its size is greater than max_size (50GB by default, max_size=0 turn off this check)
    void setMaxTableSizeToDrop(size_t max_size);
    void checkTableCanBeDropped(const String & database, const String & table, size_t table_size);

    /// Lets you select the compression method according to the conditions described in the configuration file.
    CompressionMethod chooseCompressionMethod(size_t part_size, double part_size_ratio) const;

    /// Get the server uptime in seconds.
    time_t getUptimeSeconds() const;

    void shutdown();

    enum class ApplicationType
    {
        SERVER,         /// The program is run as clickhouse-server daemon (default behavior)
        CLIENT,         /// clickhouse-client
        LOCAL_SERVER    /// clickhouse-local
    };

    ApplicationType getApplicationType() const;
    void setApplicationType(ApplicationType type);

    /// Set once
    String getDefaultProfileName() const;
    void setDefaultProfileName(const String & name);

    /// User name and session identifier. Named sessions are local to users.
    using SessionKey = std::pair<String, String>;

private:
    /** Check if the current client has access to the specified database.
      * If access is denied, throw an exception.
      * NOTE: This method should always be called when the `shared->mutex` mutex is acquired.
      */
    void checkDatabaseAccessRights(const std::string & database_name) const;

    const EmbeddedDictionaries & getEmbeddedDictionariesImpl(bool throw_on_error) const;
    const ExternalDictionaries & getExternalDictionariesImpl(bool throw_on_error) const;

    StoragePtr getTableImpl(const String & database_name, const String & table_name, Exception * exception) const;

    SessionKey getSessionKey(const String & session_id) const;

    /// Session will be closed after specified timeout.
    void scheduleCloseSession(const SessionKey & key, std::chrono::steady_clock::duration timeout);
};


/// Puts an element into the map, erases it in the destructor.
/// If the element already exists in the map, throws an exception containing provided message.
class DDLGuard
{
public:
    /// Element name -> message.
    /// NOTE: using std::map here (and not std::unordered_map) to avoid iterator invalidation on insertion.
    using Map = std::map<String, String>;

    DDLGuard(Map & map_, std::mutex & mutex_, std::unique_lock<std::mutex> && lock, const String & elem, const String & message);
    ~DDLGuard();

private:
    Map & map;
    Map::iterator it;
    std::mutex & mutex;
};


class SessionCleaner
{
public:
    SessionCleaner(Context & context_)
        : context{context_}
    {
    }
    ~SessionCleaner();

private:
    void run();

    Context & context;

    std::mutex mutex;
    std::condition_variable cond;
    std::thread thread{&SessionCleaner::run, this};
    std::atomic<bool> quit{false};
};

}
