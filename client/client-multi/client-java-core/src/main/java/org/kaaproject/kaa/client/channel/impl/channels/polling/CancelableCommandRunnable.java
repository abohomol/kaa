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

package org.kaaproject.kaa.client.channel.impl.channels.polling;


public abstract class CancelableCommandRunnable implements CancelableRunnable {

    protected volatile Command currentCommand;
    private volatile boolean isRunning = false;

    @Override
    public void run() {
        isRunning = true;
        try {
            if (!Thread.currentThread().isInterrupted()) {
                executeCommand();
            }
        } finally {
            isRunning = false;
            synchronized(this) {
                this.notifyAll();
            }
        }
    }

    public void waitUntilExecuted() {
        while (isRunning) {
            synchronized(this) {
                try {
                    this.wait();
                } catch (InterruptedException e) { //NOSONAR
                }
            }
        }
    }

    @Override
    public void cancel() {
        if (currentCommand != null) {
            currentCommand.cancel();
        }
    }

    protected abstract void executeCommand();

}
