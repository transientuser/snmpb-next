/*
    Copyright (C) 2004-2011 Martin Jolicoeur (snmpb1@gmail.com) 

    This file is part of the SnmpB project 
    (http://sourceforge.net/projects/snmpb)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef DISCOVERY_H
#define DISCOVERY_H

#include <qthread.h>
#include <memory>
#include "snmpb.h"
#include "discoveryscanplan.h"
#include "snmp_pp/snmp_pp.h"

class DiscoveryThread;

class DiscoverySnmp: public Snmp
{
public:
    DiscoverySnmp(int &status, const UdpAddress &addr);
    DiscoverySnmp(int &status,  const UdpAddress& addr_v4, const UdpAddress& addr_v6);
    void discover(const UdpAddress &start_addr, unsigned long long num_addr,
                  const int timeout_sec, const snmp_version version,
                  const SnmpRequestConfig &config, bool use_snmpv3_probe,
                  DiscoveryThread* thread,
                  const SnmpCancellationToken &cancellation);
};

class DiscoveryThread: public QThread
{
    Q_OBJECT

public:
    DiscoveryThread(QObject *parent);
    ~DiscoveryThread() override;
    void Configure(const DiscoveryScanPlan &plan);
    void run();
    void SendAgentInfo(Pdu pdu, UdpAddress a, snmp_version v);
    void Progress(void);
    void Abort(void);

public:
signals:
    void SendAgent(QStringList agent_info);
    void SignalStartStop(int isstart);
    void SignalProgress(int value);

protected:
    DiscoverySnmp *snmp;
    int status;
    int current_progress;
    DiscoveryScanPlan scanPlan;
    std::shared_ptr<SnmpCancellationToken> cancellation;
};

class Discovery: public QObject
{
    Q_OBJECT
    
public:
    Discovery(Snmpb *snmpb);
    ~Discovery() override;

public slots:
    void RefreshDestinationFolders(void);

protected slots:
    void Discover(void);
    void Abort(void);
    void DisplayAgent(QStringList agent_info);
    void StartStop(int isstart);
    void DisplayProgress(int value);
    void ShowAgentSettings(void);
    void AgentProfileListChange(void);
    void ContextMenu ( const QPoint &pos );
    void AddAgentToProfiles(void);

private:
    Snmpb *s;
    DiscoveryThread *dt;
    QAction *addAgentAct;
    class QComboBox *destinationFolder;
    DiscoveryScanPlan activePlan;
};

#endif /* DISCOVERY_H */

