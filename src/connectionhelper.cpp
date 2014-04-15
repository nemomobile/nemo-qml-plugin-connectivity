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
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusConnection>

#include <connman-qt5/networkmanager.h>

ConnectionHelper::ConnectionHelper(QObject *parent)
    : QObject(parent)
    , m_networkAccessManager(0)
    , m_networkConfigReady(false)
    , m_delayedAttemptToConnect(false)
    , m_detectingNetworkConnection(false)
    , netman(NetworkManagerFactory::createInstance()),
      connmanIsReady(false)
{
    connect(&m_timeoutTimer, SIGNAL(timeout()), this, SLOT(emitFailureIfNeeded()));
    m_timeoutTimer.setSingleShot(true);
    m_timeoutTimer.setInterval(300000); // 5 minutes

    connect(netman,SIGNAL(availabilityChanged(bool)),this,SLOT(connmanAvailableChanged(bool)));
    connect(netman,SIGNAL(stateChanged(QString)),this,SLOT(networkStateChanged(QString)));

    connmanIsAvailable = QDBusConnection::systemBus().interface()->isServiceRegistered("net.connman");
}

ConnectionHelper::~ConnectionHelper()
{
}

void ConnectionHelper::connmanAvailableChanged(bool available)
{
    if (available) {
        m_networkConfigReady = true;
        if (m_delayedAttemptToConnect) {
            m_delayedAttemptToConnect = false;
            attemptToConnectNetwork();
        }
    } else {
        connmanIsReady = false;
    }
    connmanIsAvailable = available;
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
    if (netman->defaultRoute() && netman->defaultRoute()->connected())
        return true;
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
    will emit
    networkConnectivityEstablished() if it succeeds, or
    networkConnectivityUnavailable() if it fails.

    If the user rejects the dialog, the connection helper emit networkConnectivityUnavailable()
    or until the request times out (5 minutes) at which point it will be emitted.
*/
void ConnectionHelper::attemptToConnectNetwork()
{
    if (!netman->defaultRoute()) {
        emitFailureIfNeeded();
        return;
    }
    // set up a timeout error emission trigger after 2 minutes, unless we manage to connect in the meantime.
    m_detectingNetworkConnection = true;
    m_timeoutTimer.start(300000);
    if (netman->defaultRoute()->state() != "online") {
        if (netman->defaultRoute()->state() == "ready") {
            // we already have an open session, but something isn't quite right.  Ensure that the
            // connection is usable (not blocked by a Captive Portal).
            performRequest(/*true*/);
        } else {
            // let's ask user for connection
            openConnectionDialog();
        }
    } else {
        // we are online and connman's online check has passed. Everything is ok
        m_detectingNetworkConnection = false;
        emit networkConnectivityEstablished();
    }
}

void ConnectionHelper::closeNetworkSession()
{
    // TODO
}

void ConnectionHelper::performRequest()
{
    // sometimes connman service can be in 'ready' state but can still be used.
    // In this case, let perform our own online check, just to be sure

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

    // We expect this request to succeed if the connection has been brought
    // online successfully.  It may fail if, for example, the interface is waiting
    // for a Captive Portal redirect, in which case we should consider network
    // connectivity to be unavailable (as it requires user intervention).
    connect(reply, SIGNAL(finished()), this, SLOT(handleCanaryRequestFinished()));
    connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(handleCanaryRequestError(QNetworkReply::NetworkError)));
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

void ConnectionHelper::openConnectionDialog()
{
    // open Connection Selector

    QDBusInterface *connSelectorInterface = new QDBusInterface(QStringLiteral("com.jolla.lipstick.ConnectionSelector"),
                                                               QStringLiteral("/"),
                                                               QStringLiteral("com.jolla.lipstick.ConnectionSelectorIf"),
                                                               QDBusConnection::sessionBus(),
                                                               this);

    connSelectorInterface->connection().connect(QStringLiteral("com.jolla.lipstick.ConnectionSelector"),
                                                QStringLiteral("/"),
                                                QStringLiteral("com.jolla.lipstick.ConnectionSelectorIf"),
                                                QStringLiteral("connectionSelectorClosed"),
                                                this,
                                                SLOT(connectionSelectorClosed(bool)));

    QList<QVariant> args;
    args.append("wlan");
    QDBusMessage reply = connSelectorInterface->callWithArgumentList(QDBus::NoBlock,
                                                                     QStringLiteral("openConnection"), args);

    if (reply.type() != QDBusMessage::ReplyMessage) {
        qWarning() << reply.errorMessage();
        serviceErrorChanged(reply.errorMessage());
    }
}

void ConnectionHelper::connectionSelectorClosed(bool b)
{
    if (!b) {
        //canceled
        emit networkConnectivityUnavailable();
    }
}

void ConnectionHelper::serviceErrorChanged(const QString &errorString)
{
    if (errorString.isEmpty())
        return;
    m_detectingNetworkConnection = false;
    emit networkConnectivityUnavailable();
}

void ConnectionHelper::networkStateChanged(const QString &state)
{
    if (state == "online") {
        m_detectingNetworkConnection = false;
        emit networkConnectivityEstablished();
    } else if (state == "idle") {
        emit networkConnectivityUnavailable();
    }
}
