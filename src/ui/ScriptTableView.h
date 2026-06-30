#ifndef SIGNALBRIDGE_SCRIPT_TABLE_VIEW_H
#define SIGNALBRIDGE_SCRIPT_TABLE_VIEW_H

#include <QStringList>

class QTableWidget;

namespace signalbridge
{
void ConfigureScriptTable(QTableWidget* table);
void SetScriptTableRows(QTableWidget* table, const QStringList& scripts, bool running);
}

#endif // SIGNALBRIDGE_SCRIPT_TABLE_VIEW_H
