# Futures — reading a market, and trading it on paper

Open **FUTURES** from the tab bar. Quotes are Yahoo's, about ten minutes
behind the exchange; nothing here needs an API key.

## The screen

| Panel | What it shows |
|---|---|
| **Board** | Every contract of the selected class, live. **MAJORS** is the first page: the contracts that set the tone for each class. Click a row to load that market. |
| **Strip** | The front month's price, the curve's shape (contango / backwardation), the roll yield, and how many days the front month has left. It turns amber within five days of the last trading day. For Fed Funds and SOFR it says how many basis points lower or higher the rate implied by the far month (about a year out, or the last month trading) is than the front month's. |
| **Chart** | The continuous front-month series — or one delivery month, when you click it on the curve or in the table. The continuous series jumps at every roll; a single month does not. |
| **Forward curve** | Every listed month now, against the previous close and against where the same months settled a week and a month ago. |
| **Contract months** | Last, change, spread to the front, annualised carry, volume, open interest, last trading day. Fed Funds and SOFR show the rate each month implies. |
| **Spread** | Six months of the calendar spread between the front month and the month you picked, and where today's value sits in that range. |
| **Positioning** | The CFTC's weekly Commitments of Traders, three years: large speculators, commercials, small traders. |
| **Ticket** | Size a trade in dollars, then fill it in the paper account. |
| **Contract** | Size, quote unit, tick and its dollar value. |

## Things worth knowing

- **Hollow points and dimmed rows** are months that have not traded since the
  session before the newest print. Their price is an old trade, so they are
  left out of the curve's line, the spreads and the carry.
- **Roll yield** is what holding the front month and rolling into the next
  earns a long, a year: positive in backwardation, negative in contango.
- **Grains, softs and livestock are quoted in cents.** The ticket's dollar
  figures already account for that (corn at 450 is $22,500 a contract).
- **Margin is not shown anywhere.** Exchanges revise it often and there is no
  feed for it; ask the broker.

## The paper account

A simulated $250,000 account ("Futures Paper"). Orders fill at the contract's
last print with no commission; margin is modelled at 5% of notional, which is
not any exchange's requirement. A stop or target typed on the ticket stays
with the position it opened — not with the contract: close or reverse the
position by any route and its levels go with it. The position is closed at
the first print at or beyond a level — not at the level itself, so a gap
fills worse — and only on a fresh print while this screen is open. A stale
month's old price neither marks a position nor closes one. Levels typed with
an order that only reduces or reverses a position are not kept. A position in a month close to its last trading day carries a
warning to close or roll it.

## For agents

Three MCP tools read the same data: `list_futures_contracts`,
`get_futures_curve` and `get_futures_positioning`.
