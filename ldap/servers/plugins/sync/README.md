With support from a techwriter at SUSE, we managed to translate RCF4533
into something we can understand.

* Server issues Sync Operation to search.
* Search returns entries
* Each entry has a state add which includes the UUID of the entry
* Distinguished names can change, but UUIDs are stable.
* At the end of the search results, a sync done control cookie is sent to indicate that the session is done.

* Clients issue a Sync Operation to search, and include previously returned cookie.
* Server determines what would be returned in a normal search operation.
* Server uses cookie to work out what was previously returned.
* Server issues results of only the things that have changed since last search.
* Each result sent also includes info about the status (changed or unchanged)

* Server has two phases to sync deleted entries: present or delete.
* Each phase ends with a sync done control cookie.
* The present phase ends with sync done control value of searchresultdone=false
* the delete phase ends with sync done control value of searchresultdone=true
* The present phase can be followed by the delete phase.
* Each phase is complete only after a sync info message of refreshdone=false

* During the present phase, the server sends an empty entry with state=present for each unchanged entry.
* During the present phase, the client is changed to match the server state, in preparation for the delete phase.
* During the delete phase, the server determines which entries in the client copy are no longer present. It also checks that the the number of changed entries is less than the number of unchanged entries.
 For each entry that is no longer present, the server sends a state=delete. It does not* return a state=present for each present entry.

* The server can send sync info messages that contain the list of UUIDs of either unchanged present entries, or deleted entries. This is instead of sending individual messages for each entry.
* If refreshDeletes=false the UUIDs of unchanged present entries are included in the syncUUIDs set.
* If refreshDeletes=true the UUIDs of unchanged deleted entries are included in the syncUUIDs set.
* Optionally, you can include a syncIdSet cookie to indicate the state of the content in the syncUUIDs set.

* If the syncDoneValue is refreshDeletes=false the new copy includes:
- All changed entries returned by the current sync
- All unchanged entries from a previous sync that have been confirmed to still be present
- Unchanged entries confirmed as deleted are deleted from the client. In this case, they are assumed to have been deleted or moved.

* If the syncDoneValue is refreshDeletes=true the new copy includes:
- All changed entries returned by the current sync
- All entries from a previous sync that have not been marked as deleted.

* Clients can request that the synchronized copy is refreshed at any time.
