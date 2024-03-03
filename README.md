# SRW-2

[AcquireSRWLockShared](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-acquiresrwlockshared) some time can really acquires a slim SRW lock in **exclusive** mode !

steps to reproduce the situation

- main thread  start N (> 1) worker threads
- the N-1 threads is started normal and we give them time to call `AcquireSRWLockShared` by using `Sleep(1000)` and one thread is started in suspended state 
- then we call `ReleaseSRWLockExclusive` from the main thread


and with the help of VEX we catch the moment when R`eleaseSRWLockExclusive` set the [K bit](https://github.com/mic101/windows/blob/master/WRK-v1.2/base/ntos/ex/pushlock.c#L31) in SRW
here we suspend the main thread and resume the last worker. and give it time (again `Sleep(1000)`) to enter to the lock. and it enter in - `AcquireSRWLockShared` works successfully - since the **L**ock bit has already been removed
then, we continue executing the main thread in `ReleaseSRWLockExclusive`

and here we have after this: 1 worker thread inside SRW shows a message box and another N-1 worker threads is waiting inside `AcquireSRWLockShared`

this is repro in crystal clear form - we have a thread that requested shared and received exclusive access to SRW

But ! as soon as we close the messagebox and release SRW, N-1 messageboxes will immediately appear then.
nothing stuck. no deadlock.

and if the code was written correctly, such behavior would not cause any problems. we got MORE than we asked for. but so what ? did we ask for shared access? we got it. then we must work with the data that this SRW protect and exit. 
and everything will be ok. no one will even notice anything. visa versa - if we asked for exclusive and got shared - this is a critical error and can lead to data corruption

