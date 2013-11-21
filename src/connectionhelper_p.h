/* Copyright (C) 2013 Jolla Ltd.
 * Contact: Chris Adams <chris.adams@jolla.com>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * "Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Jolla Ltd. nor the names of its contributors
 *     may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
 */

#include <QObject>

#include <QNetworkConfigurationManager>
#include <QNetworkSession>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include <connman-qt5/networkmanager.h>
#include <connman-qt5/networktechnology.h>
#include <connman-qt5/networkservice.h>

#include <QTimer>

class ConnectionHelper : public QObject
{
    Q_OBJECT

public:
    ConnectionHelper(QObject *parent = 0);
    ~ConnectionHelper();

    Q_INVOKABLE bool haveNetworkConnectivity() const;
    Q_INVOKABLE void attemptToConnectNetwork();
    Q_INVOKABLE void closeNetworkSession();

Q_SIGNALS:
    void networkConnectivityEstablished();
    void networkConnectivityUnavailable();

private Q_SLOTS:
    void networkConfigurationUpdateCompleted();
    void performRequest(bool expectSuccess);

    void handleCanaryRequestError(const QNetworkReply::NetworkError &error);
    void handleCanaryRequestFinished();
    void emitFailureIfNeeded(); // due to timeout.

    void connmanAvailableChanged(bool);
    void defaultSessionConnectedChanged(bool);
    void serviceErrorChanged(const QString &);

private:
    QTimer m_timeoutTimer;
    QNetworkConfigurationManager *m_networkConfigManager;
    QNetworkSession *m_networkDefaultSession;
    QNetworkAccessManager *m_networkAccessManager;
    bool m_networkConfigReady;
    bool m_delayedAttemptToConnect;
    bool m_detectingNetworkConnection;

    NetworkManager *netman;
    NetworkService *defaultService;

    bool askRoaming() const;
};
