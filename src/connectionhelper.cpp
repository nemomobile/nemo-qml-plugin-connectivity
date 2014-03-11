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

#include "connectionhelper_p.h"

#include <QTimer>
#include <QUrl>
#include <QString>

ConnectionHelper::ConnectionHelper(QObject *parent)
    : QObject(parent)
    , m_networkConfigManager(new QNetworkConfigurationManager(this))
    , m_networkDefaultSession(0)
    , m_networkAccessManager(0)
    , m_networkConfigReady(false)
    , m_delayedAttemptToConnect(false)
    , m_detectingNetworkConnection(false)
{
    connect(&m_timeoutTimer, SIGNAL(timeout()), this, SLOT(emitFailureIfNeeded()));
    m_timeoutTimer.setSingleShot(true);
    m_timeoutTimer.setInterval(120000); // 2 minutes

    connect(m_networkConfigManager, SIGNAL(updateCompleted()),
            this, SLOT(networkConfigurationUpdateCompleted()), Qt::QueuedConnection);
    connect(m_networkConfigManager, SIGNAL(onlineStateChanged(bool)),
            this, SLOT(recheckDefaultConnection(bool)), Qt::QueuedConnection);
    m_networkConfigManager->updateConfigurations();
}

ConnectionHelper::~ConnectionHelper()
{
    closeNetworkSession();
}

void ConnectionHelper::networkConfigurationUpdateCompleted()
{
    m_networkConfigReady = true;
    if (m_delayedAttemptToConnect) {
        m_delayedAttemptToConnect = false;
        attemptToConnectNetwork();
    }
}

/*
    Closes the network session which is being held open by
    the ConnectionHelper.  Note that if something else is
    holding an open session with the default configuration,
    the network configuration will remain online.
*/
void ConnectionHelper::closeNetworkSession()
{
    if (m_networkDefaultSession) {
        m_networkDefaultSession->close();
        m_networkDefaultSession->deleteLater();
        m_networkDefaultSession = 0;
    }
}

/*
    Checks whether the default network configuration is currently
    connected.  Note that the default configuration may be
    connected even if the ConnectionHelper has not yet created
    a session with that configuration.

    Note that this function will return true if the network is
    connected or available, even if the network is not immediately
    usable!  For example, an available network might have a captive
    portal set for it (which requires user intervention via web
    browser before the connection can be used for other data).

    As such, clients are advised to use attemptToConnectNetwork()
    if they need to know whether the network is usable.

    This function is most useful for clients who want to simply
    disable some part of their functionality if the network is not
    currently connected.
*/
bool ConnectionHelper::haveNetworkConnectivity() const
{
    if (m_networkDefaultSession && m_networkDefaultSession->isOpen()) {
        return true;
    }
    QNetworkConfiguration defaultConfig = m_networkConfigManager->defaultConfiguration();
    if (defaultConfig.state() == QNetworkConfiguration::Active
            || defaultConfig.state() == QNetworkConfiguration::Discovered) {
        return true;
    }
    return false;
}

/*
    Attempts to perform a network request.
    If it succeeds, the user has connected to a network.
    If it fails, the user has explicitly denied the network request.
    Emits networkConnectivityEstablished() if the request succeeds.

    Note that if no valid network configuration exists, the user will
    be prompted to add a valid network configuration (eg, connect to wlan).

    If the user does add a valid configuration, the connection helper
    will then attempt to connect with that configuration and will emit
    networkConnectivityEstablished() if it succeeds, or
    networkConnectivityUnavailable() if it fails.

    If the user rejects the dialog, the connection helper will not know
    and so will NOT emit networkConnectivityUnavailable() until the
    request times out (2 minutes) at which point it will be emitted.
*/
void ConnectionHelper::attemptToConnectNetwork()
{
    // set up a timeout error emission trigger after 2 minutes, unless we manage to connect in the meantime.
    m_detectingNetworkConnection = true;
    m_timeoutTimer.start(120000);

    // attempt to check the default network configuration.
    if (!m_networkConfigReady) {
        // we need to queue up attemptToConnectNetwork() once we've finished scanning network configurations.
        m_delayedAttemptToConnect = true;
        return;
    }

    if (!m_networkDefaultSession) {
        QNetworkConfiguration defaultConfig = m_networkConfigManager->defaultConfiguration();
        if (!defaultConfig.isValid()
                || !defaultConfig.state().testFlag(QNetworkConfiguration::Active)
                && !defaultConfig.state().testFlag(QNetworkConfiguration::Discovered)) {
            performRequest(false); // dummy request to trigger network connection dialog.
        } else {
            m_networkDefaultSession = new QNetworkSession(defaultConfig);

            // note that we re-create the signal connections each time
            // this is so that we can avoid spurious signal emissions
            // due to connection state changes.
            connect(m_networkDefaultSession, SIGNAL(error(QNetworkSession::SessionError)),
                    this, SLOT(handleNetworkSessionError()));
            connect(m_networkDefaultSession, SIGNAL(opened()),
                    this, SLOT(handleNetworkSessionOpened()));
            m_networkDefaultSession->open();
        }
    } else {
        // we already have an open session.  Ensure that the
        // connection is usable (not blocked by a Captive Portal).
        performRequest(true);
    }
}

void ConnectionHelper::recheckDefaultConnection(bool isOnline)
{
    // The online state of the connection manager changed.
    // This means that the network may now be available
    // or unavailable, depending on circumstances.
    // Find out which it is, and emit appropriately.
    if (!isOnline) {
        // probably already false/closed, but just in case...
        m_detectingNetworkConnection = false;
        closeNetworkSession();

        // emit networkConnectivityUnavailable, in case clients are depending on it.
        QMetaObject::invokeMethod(this, "networkConnectivityUnavailable", Qt::QueuedConnection);
        return;
    }

    // a connection has been added, so the default configuration may have changed.
    QNetworkConfiguration defaultConfig = m_networkConfigManager->defaultConfiguration();
    if (!defaultConfig.isValid()) {
        // even after connecting, we still don't have a valid default configuration.  Fail.
        m_networkConfigReady = false;
        m_networkConfigManager->updateConfigurations();
        QMetaObject::invokeMethod(this, "networkConnectivityUnavailable", Qt::QueuedConnection);
        return;
    }

    // If isOnline is true, then we have access to a network
    // but we don't know whether it's usable.  It may be blocked
    // by a Captive Portal, for example.  Find out.
    if (m_networkDefaultSession) {
        if (m_networkDefaultSession->isOpen()) {
            // connected in the meantime.  Attempt a "real" network request to check usability.
            performRequest(true);
            return;
        } else if (m_networkDefaultSession->configuration() == defaultConfig) {
            // the default configuration hasn't changed, but perhaps it
            // is now available where before it was not.  Attempt to connect.

            // note that we re-create the signal connections each time
            // this is so that we can avoid spurious signal emissions
            // due to connection state changes which occur later.
            connect(m_networkDefaultSession, SIGNAL(error(QNetworkSession::SessionError)),
                    this, SLOT(handleNetworkSessionError()));
            connect(m_networkDefaultSession, SIGNAL(opened()),
                    this, SLOT(handleNetworkSessionOpened()));
            m_networkDefaultSession->open();
            return;
        }
    }

    // we need to recreate the default session to ensure that the correct
    // configuration is used.
    closeNetworkSession();

    // After triggering the dummy request, we have a new network connection.
    // We now detect the state of that connection - if we can bring it online,
    // then we emit networkConnectivityEstablished(); otherwise, we emit
    // networkConnectivityUnavailable().
    m_networkDefaultSession = new QNetworkSession(defaultConfig);

    // note that we re-create the signal connections each time
    // this is so that we can avoid spurious signal emissions
    // due to connection state changes which occur later.
    connect(m_networkDefaultSession, SIGNAL(error(QNetworkSession::SessionError)),
            this, SLOT(handleNetworkSessionError()));
    connect(m_networkDefaultSession, SIGNAL(opened()),
            this, SLOT(handleNetworkSessionOpened()));
    m_networkDefaultSession->open();
}

void ConnectionHelper::performRequest(bool expectSuccess)
{
    // The QNetworkAccessManager of the QML engine always uses the
    // default connection (unless it was changed via the factory hook).
    // If the default connection is not valid, it means that the user
    // has not yet connected to a network (wireless LAN or cellular data).
    // In order to prompt the user to add a connection, we perform a
    // "dummy" get request using a QNetworkAccessManager.
    if (!m_networkAccessManager) {
        m_networkAccessManager = new QNetworkAccessManager(this);
    }

    // Testing network connectivity, always load from network.
    QNetworkRequest request (QUrl(QStringLiteral("http://ipv4.jolla.com/online/status.html")));
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    QNetworkReply *reply = m_networkAccessManager->head(request);
    if (!reply) {
        // couldn't create request / pop up connection dialog.
        m_detectingNetworkConnection = false;
        QMetaObject::invokeMethod(this, "networkConnectivityUnavailable", Qt::QueuedConnection);
        return;
    }

    if (!expectSuccess) {
        // We expect this request to fail, since it will take the user some time
        // to select the network they wish to connect to, and connect to it.
        // However, some time after this request fails, we should see the online
        // state of the QNetworkConfigurationManager change, which will trigger
        // the recheckDefaultConnection() slot.
        connect(reply, SIGNAL(finished()), this, SLOT(handleDummyRequestFinished()));
    } else {
        // We expect this request to succeed if the connection has been brought
        // online successfully.  It may fail if, for example, the interface is waiting
        // for a Captive Portal redirect, in which case we should consider network
        // connectivity to be unavailable (as it requires user intervention).
        connect(reply, SIGNAL(finished()), this, SLOT(handleCanaryRequestFinished()));
        connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(handleCanaryRequestError(QNetworkReply::NetworkError)));
    }
}

void ConnectionHelper::handleDummyRequestFinished()
{
    QNetworkReply *dummyReply = qobject_cast<QNetworkReply*>(sender());
    dummyReply->deleteLater();
}

void ConnectionHelper::handleNetworkSessionError()
{
    m_detectingNetworkConnection = false;
    m_networkDefaultSession->disconnect();
    emit networkConnectivityUnavailable();
}

void ConnectionHelper::handleNetworkSessionOpened()
{
    // we have an open session with the default connection.
    // attempt a "real" request to determine the usability
    // of the connection.
    m_networkDefaultSession->disconnect(); // not interested in signals from it now.
    performRequest(true);
}

void ConnectionHelper::handleCanaryRequestError(const QNetworkReply::NetworkError &)
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    reply->setProperty("isError", QVariant::fromValue<bool>(true));
    reply->deleteLater();
    m_detectingNetworkConnection = false;
    QMetaObject::invokeMethod(this, "networkConnectivityUnavailable", Qt::QueuedConnection);
    closeNetworkSession();
}

void ConnectionHelper::handleCanaryRequestFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply->property("isError").toBool()) {
        reply->deleteLater();
        m_detectingNetworkConnection = false;
        emit networkConnectivityEstablished();
    }
}

void ConnectionHelper::emitFailureIfNeeded()
{
    // unless a successful connection was established since the call to this function
    // was queued, we should emit the error signal.
    if (m_detectingNetworkConnection) {
        m_detectingNetworkConnection = false;
        QMetaObject::invokeMethod(this, "networkConnectivityUnavailable", Qt::QueuedConnection);
    }
}
