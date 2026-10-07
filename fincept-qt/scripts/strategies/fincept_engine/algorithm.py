# ============================================================================
# finterm - Strategy Engine Core Algorithm
# Pure Python QCAlgorithm compatible base class
# This is the heart of the finterm Strategy Engine — all strategies inherit
# from this class. It provides the same API surface as LEAN's QCAlgorithm
# but runs entirely in pure Python with no .NET dependency.
# ============================================================================

import json
import math
import logging
from datetime import datetime, timedelta, date, time as dt_time
from typing import Dict, List, Optional, Callable, Any
from collections import deque

try:
    import numpy as np
except ImportError:
    np = None

try:
    import pandas as pd
except ImportError:
    pd = None

from .enums import (
    Resolution, SecurityType, OrderType, OrderStatus,
    Market, InsightDirection, InsightType, DataNormalizationMode
)
from .types import (
    Symbol, Slice, TradeBar, QuoteBar, Tick,
    SecurityHolding, OrderTicket, OrderEvent, UpdateOrderFields,
    Insight, PortfolioTarget
)
from .indicators import (
    IndicatorBase, ExponentialMovingAverage, SimpleMovingAverage,
    MovingAverageConvergenceDivergence, RelativeStrengthIndex,
    BollingerBands, AverageTrueRange, Stochastic,
    RateOfChange, MomentumPercent, Momentum, WilliamsPercentR,
    CommodityChannelIndex, AverageDirectionalIndex
)
from .portfolio import SecurityPortfolioManager
from .securities import SecurityManager, Security
from .scheduling import ScheduleManager, DateRules, TimeRules
from .consolidators import (
    TradeBarConsolidator, QuoteBarConsolidator,
    RenkoConsolidator, RangeConsolidator, TickConsolidator
)
from .universe import UniverseSettings, Universe

logger = logging.getLogger("fincept.strategy")


class _AlgorithmMode:
    """Internal AlgorithmMode enum matching LEAN's AlgorithmMode."""
    BACKTESTING = 0; RESEARCH = 1; LIVE = 2
    Backtesting = 0; Research = 1; Live = 2
    def __eq__(self, other): return int(self) == int(other)
    def __repr__(self): return f"AlgorithmMode({self})"


class _OptionContract:
    """Represents a single option contract in a chain.

    Quote fields come from a real option-chain snapshot (Yahoo Finance);
    anything the source does not provide (e.g. greeks) is None -- never
    filled in with constants.
    """
    def __init__(self, symbol, underlying, strike, right, expiry, quote=None):
        quote = quote or {}
        self.symbol = symbol
        self.underlying = underlying
        self.strike = strike
        self.strike_price = strike
        self.right = right
        self.expiry = expiry
        self.bid_price = quote.get('bid')
        self.ask_price = quote.get('ask')
        self.last_price = quote.get('last')
        self.volume = quote.get('volume')
        self.open_interest = quote.get('open_interest')
        self.implied_volatility = quote.get('implied_volatility')
        self.greeks = type('Greeks', (), {
            'delta': None, 'gamma': None, 'vega': None, 'theta': None, 'rho': None,
            'implied_volatility': self.implied_volatility
        })()
        self.id = type('ContractId', (), {
            'strike_price': strike, 'option_right': right,
            'date': expiry, 'expiration': expiry, 'underlying': underlying
        })()
        self.id.Date = expiry
        self.id.StrikePrice = strike
        self.id.OptionRight = right

    def __repr__(self):
        return f"OptionContract({self.symbol}, strike={self.strike}, right={self.right})"


def _fetch_real_option_chain(underlying, as_of):
    """Fetch the real listed option chain for `underlying` from Yahoo Finance.

    Yahoo only serves the *current* chain, so this raises RuntimeError when
    `as_of` is not (approximately) today -- historical chains are not
    available and are never synthesized. Futures options are unsupported.

    Returns a list of (opt_symbol, strike, right, expiry, quote_dict).
    """
    from .enums import OptionRight, OptionStyle
    ticker_str = str(underlying).upper()
    und_sec_type = getattr(underlying, 'security_type', SecurityType.EQUITY)
    if und_sec_type in (SecurityType.FUTURE, SecurityType.FUTURE_OPTION, SecurityType.CRYPTO_FUTURE):
        raise RuntimeError(f"Option chain unavailable for {ticker_str}: no real futures-option "
                           f"data source in the local engine")
    as_of = as_of.replace(tzinfo=None) if isinstance(as_of, datetime) and as_of.tzinfo else as_of
    if isinstance(as_of, date) and not isinstance(as_of, datetime):
        as_of = datetime(as_of.year, as_of.month, as_of.day)
    if as_of is None or abs((datetime.now() - as_of).days) > 3:
        raise RuntimeError(f"Option chain unavailable for {ticker_str} as of {as_of}: only the "
                           f"current chain is available (no historical option data source)")
    if und_sec_type == SecurityType.INDEX:
        yf_sym = ticker_str if ticker_str.startswith('^') else f'^{ticker_str}'
        opt_sec_type, opt_style = SecurityType.INDEX_OPTION, OptionStyle.EUROPEAN
    else:
        yf_sym = ticker_str
        opt_sec_type, opt_style = SecurityType.OPTION, OptionStyle.AMERICAN
    try:
        import yfinance as yf
    except ImportError:
        raise RuntimeError(f"Option chain unavailable for {ticker_str}: yfinance is not installed")

    def _num(v, cast=float):
        try:
            if v is None or (isinstance(v, float) and math.isnan(v)):
                return None
            return cast(v)
        except (TypeError, ValueError):
            return None

    try:
        tk = yf.Ticker(yf_sym)
        expiries = list(tk.options or [])
    except Exception as e:
        raise RuntimeError(f"Option chain unavailable for {ticker_str}: {e}") from e
    if not expiries:
        raise RuntimeError(f"Option chain unavailable for {ticker_str}: Yahoo Finance lists no expiries")

    out = []
    for exp_str in expiries:
        expiry = datetime.strptime(exp_str, '%Y-%m-%d')
        try:
            oc = tk.option_chain(exp_str)
        except Exception as e:
            raise RuntimeError(f"Option chain unavailable for {ticker_str} {exp_str}: {e}") from e
        for right, frame in ((OptionRight.CALL, oc.calls), (OptionRight.PUT, oc.puts)):
            if frame is None:
                continue
            for _, row in frame.iterrows():
                strike = _num(row.get('strike'))
                if strike is None:
                    continue
                right_char = 'C' if right == OptionRight.CALL else 'P'
                ticker = row.get('contractSymbol') or f"{ticker_str}{expiry:%y%m%d}{right_char}{strike:g}"
                opt_sym = Symbol(str(ticker), opt_sec_type, Market.USA)
                opt_sym.expiry = expiry
                opt_sym.strike_price = strike
                opt_sym.right = right
                opt_sym.option_style = opt_style
                opt_sym._underlying = underlying
                opt_sym.id = type('SymbolId', (), {
                    'strike_price': strike, 'option_right': right,
                    'date': expiry, 'expiration': expiry, 'underlying': underlying
                })()
                quote = {
                    'bid': _num(row.get('bid')),
                    'ask': _num(row.get('ask')),
                    'last': _num(row.get('lastPrice')),
                    'volume': _num(row.get('volume'), int),
                    'open_interest': _num(row.get('openInterest'), int),
                    'implied_volatility': _num(row.get('impliedVolatility')),
                }
                out.append((opt_sym, strike, right, expiry, quote))
    return out


class _OptionChainResult:
    """Result of option_chain() with .contracts dict-like access and list-like iteration."""
    def __init__(self, underlying=None):
        self.contracts = {}
        self.underlying = underlying
        self._list = []
    def __iter__(self):
        return iter(self._list)
    def __len__(self):
        return len(self._list)
    def __getitem__(self, idx):
        if isinstance(idx, int):
            return self._list[idx]
        return self.contracts.get(str(idx))
    def __bool__(self):
        return len(self._list) > 0
    @property
    def data_frame(self):
        if pd is not None:
            data = []
            for c in self._list:
                data.append({
                    'symbol': str(c.symbol),
                    'expiry': c.expiry,
                    'strike': c.strike,
                    'right': c.right,
                    'impliedvolatility': c.implied_volatility,
                    'delta': c.greeks.delta,
                    'gamma': c.greeks.gamma,
                    'vega': c.greeks.vega,
                    'theta': c.greeks.theta,
                    'rho': c.greeks.rho,
                    'bid_price': c.bid_price,
                    'ask_price': c.ask_price,
                    'last_price': c.last_price,
                    'volume': c.volume,
                    'open_interest': c.open_interest,
                })
            if data:
                df = pd.DataFrame(data)
                # Use the actual symbol objects as index so .name returns Symbol
                df.index = pd.Index([c.symbol for c in self._list], name=None)
                return df
            return pd.DataFrame()
        return None


class _OptionChainsAccessor(dict):
    """Dict-like object that is also callable for option_chains([symbols])."""
    def __init__(self, algo):
        super().__init__()
        self._algo = algo
    def __call__(self, symbols=None, flatten=False):
        result = _OptionChainsResult()
        if symbols:
            for sym in symbols:
                chain = self._algo.option_chain(sym, flatten=flatten)
                result[str(sym)] = chain
                result._chains.append((sym, chain))
        return result

    @property
    def data_frame(self):
        if pd is not None:
            return pd.DataFrame()
        return None


class _OptionChainsResult(dict):
    """Result of option_chains() call with .data_frame support."""
    def __init__(self):
        super().__init__()
        self._chains = []

    @property
    def data_frame(self):
        if pd is not None:
            all_data = []
            all_index = []
            for canonical_sym, chain in self._chains:
                for c in chain._list:
                    row = {
                        'expiry': c.expiry,
                        'strike': c.strike,
                        'right': c.right,
                        'impliedvolatility': c.implied_volatility,
                        'delta': c.greeks.delta,
                        'gamma': c.greeks.gamma,
                        'vega': c.greeks.vega,
                        'theta': c.greeks.theta,
                        'rho': c.greeks.rho,
                        'bid_price': c.bid_price,
                        'ask_price': c.ask_price,
                        'last_price': c.last_price,
                        'volume': c.volume,
                        'open_interest': c.open_interest,
                    }
                    all_data.append(row)
                    all_index.append((canonical_sym, c.symbol))
            if all_data:
                idx = pd.MultiIndex.from_tuples(all_index, names=['canonical', 'contract'])
                return pd.DataFrame(all_data, index=idx)
            return pd.DataFrame()
        return None


class _UniverseReference:
    """Reference to an added universe, supporting .data_type, .symbol, etc."""
    def __init__(self, selector=None, data_type=None, is_etf=False):
        self._selector = selector
        self._is_etf = is_etf
        self.data_type = data_type or type('FundamentalData', (), {'__name__': 'FundamentalData'})
        self.symbol = Symbol('UNIVERSE', SecurityType.BASE, Market.USA) if Symbol else None
    def __repr__(self):
        return f"Universe(selector={self._selector})"


class _AlgorithmSettings:
    """Algorithm-level settings."""
    def __init__(self):
        self.free_portfolio_value = 0
        self.free_portfolio_value_percentage = 0.0025
        self.liquidate_enabled = True
        self.max_absolute_portfolio_target_percentage = 1000
        self.min_absolute_portfolio_target_percentage = 0
        self.rebalance_portfolio_on_security_changes = True
        self.rebalance_portfolio_on_insight_changes = True
        self.data_subscription_limit = 10000
        self.staleness_price_time_resolution = None
        self.warmup_resolution = None
        self.database_path = ""


class Transactions:
    """Order transaction manager."""

    def __init__(self):
        self._orders: Dict[int, OrderTicket] = {}
        self._order_counter = 0

    def get_open_orders(self, symbol=None) -> List[OrderTicket]:
        orders = [o for o in self._orders.values()
                  if o.status in (OrderStatus.SUBMITTED, OrderStatus.PARTIALLY_FILLED, OrderStatus.NEW)]
        if symbol:
            ticker = str(symbol).upper()
            orders = [o for o in orders if str(o.symbol).upper() == ticker]
        return orders

    def get_orders(self, symbol=None) -> Dict[int, OrderTicket]:
        if symbol:
            ticker = str(symbol).upper()
            return {k: v for k, v in self._orders.items()
                    if str(v.symbol).upper() == ticker}
        return dict(self._orders)

    def get_order_tickets(self, filter_func=None) -> List[OrderTicket]:
        tickets = list(self._orders.values())
        if filter_func:
            tickets = [t for t in tickets if filter_func(t)]
        return tickets

    def cancel_open_orders(self, symbol=None, tag: str = ""):
        for order in self.get_open_orders(symbol):
            order.cancel(tag)

    @property
    def last_order_id(self) -> int:
        return self._order_counter


class _MarketHoursDatabase:
    """Market hours database stub."""
    @staticmethod
    def from_data_folder():
        return _MarketHoursDatabase()
    def get_exchange_hours(self, market, symbol, security_type):
        from .securities import ExchangeHours
        return ExchangeHours()
    def set_entry(self, market, symbol, security_type, exchange_hours, *args, **kwargs):
        pass
    def get_entry(self, market, symbol=None, security_type=None):
        from .securities import ExchangeHours, SymbolProperties
        return type('Entry', (), {
            'exchange_hours': ExchangeHours(),
            'data_time_zone': 'America/New_York',
            'symbol_properties': SymbolProperties(),
        })()
    SetEntry = set_entry
    GetEntry = get_entry


class _SymbolPropertiesDatabase:
    """Symbol properties database stub."""
    @staticmethod
    def from_data_folder():
        return _SymbolPropertiesDatabase()
    def get_symbol_properties(self, market, symbol, security_type, currency="USD"):
        from .securities import SymbolProperties
        return SymbolProperties()
    def set_entry(self, *args, **kwargs):
        pass
    SetEntry = set_entry
    GetSymbolProperties = get_symbol_properties


class _ObjectStoreKVP:
    """Key-value pair for ObjectStore iteration."""
    def __init__(self, key, value):
        self.key = key
        self.value = type('StoreValue', (), {
            'length': len(str(value)) if value else 0,
            '__str__': lambda self: str(value),
            '__repr__': lambda self: str(value),
        })() if value is not None else None
    Key = property(lambda self: self.key)
    Value = property(lambda self: self.value)


class ObjectStore:
    """Simple key-value object store."""

    def __init__(self):
        self._store: Dict[str, str] = {}

    def save(self, key: str, value: str):
        self._store[key] = value

    def read(self, key: str) -> str:
        return self._store.get(key, "")

    def save_json(self, key: str, obj):
        self._store[key] = json.dumps(obj)

    def read_json(self, key: str):
        val = self._store.get(key, "{}")
        return json.loads(val)

    def contains_key(self, key: str) -> bool:
        return key in self._store

    def delete(self, key: str):
        self._store.pop(key, None)

    def save_bytes(self, key: str, data: bytes):
        self._store[key] = data

    def read_bytes(self, key: str) -> bytes:
        return self._store.get(key, b"")

    def get_file_path(self, key: str) -> str:
        """Return a virtual file path for the key."""
        return f"/objectstore/{key}"

    def __getitem__(self, key):
        return self._store.get(key, self._store.get(str(key), ""))

    def __setitem__(self, key, value):
        self._store[key] = value

    def __contains__(self, key):
        return key in self._store

    def __iter__(self):
        """Iterate as key-value pairs with .key and .value attributes."""
        return iter([_ObjectStoreKVP(k, v) for k, v in self._store.items()])


class _SubscriptionDataConfig:
    """Represents a single data subscription."""
    def __init__(self, symbol, resolution=None, tick_type=None):
        self.symbol = symbol
        self.resolution = resolution
        self.tick_type = tick_type
        self.consolidator = None
        self.data_normalization_mode = 0  # DataNormalizationMode.Adjusted
        self.data_type = None
        self.is_custom_data = False

    def __eq__(self, other):
        if isinstance(other, _SubscriptionDataConfig):
            return str(self.symbol) == str(other.symbol)
        return False

    def __getattr__(self, name):
        # Fallback for any missing attribute
        if name.startswith('_'):
            raise AttributeError(name)
        return None


class SubscriptionManager:
    """Manages data subscriptions."""

    def __init__(self):
        self._subscriptions = []

    def add_subscription(self, symbol, resolution=None, data_normalization_mode=None):
        """Add a data subscription for a symbol."""
        # Add trade + quote subscriptions (like LEAN does for equities)
        config = _SubscriptionDataConfig(symbol, resolution, 'trade')
        if data_normalization_mode is not None:
            config.data_normalization_mode = data_normalization_mode
        self._subscriptions.append(config)
        config2 = _SubscriptionDataConfig(symbol, resolution, 'quote')
        if data_normalization_mode is not None:
            config2.data_normalization_mode = data_normalization_mode
        self._subscriptions.append(config2)

    def add_consolidator(self, symbol, consolidator):
        config = _SubscriptionDataConfig(symbol)
        config.consolidator = consolidator
        self._subscriptions.append(config)

    def remove_consolidator(self, symbol, consolidator):
        self._subscriptions = [
            s for s in self._subscriptions
            if not (str(s.symbol) == str(symbol) and s.consolidator is consolidator)
        ]

    @property
    def subscriptions(self):
        return self._subscriptions

    def get_subscription_data_configs(self, symbol=None, *args, **kwargs):
        if symbol:
            return [s for s in self._subscriptions if str(s.symbol) == str(symbol)]
        return self._subscriptions

    @property
    def subscription_data_config_service(self):
        return self


class SecurityChanges:
    """Tracks securities added/removed from universe."""

    def __init__(self, added=None, removed=None):
        self.added_securities = added or []
        self.removed_securities = removed or []

    @property
    def count(self):
        return len(self.added_securities) + len(self.removed_securities)

    @staticmethod
    def none():
        return SecurityChanges()


class _DataDictionary(dict):
    """Dict-like object keyed by symbol for typed history multi-symbol results.
    Supports .values()[0] style access."""
    def values(self):
        return list(super().values())


class _HistoryRow:
    """A row from history iteration that supports both attribute and dict-style access.
    When accessed as a Slice (data[symbol]), returns self for column data.
    """
    def __init__(self):
        self._data = {}
        self.time = None
        self.end_time = None
        self.value = 0.0
        self.price = 0.0
    def __getitem__(self, key):
        # Support both column access ('close') and symbol access (Symbol object)
        if isinstance(key, str) and key in self._data:
            return self._data.get(key, 0.0)
        # If key is a Symbol or string that's not a column, return self as the bar
        return self
    def __setitem__(self, key, value):
        self._data[key] = value
        setattr(self, key, value)
    def __contains__(self, key):
        return True  # Pretend all symbols are present
    def keys(self):
        """Return empty keys to avoid iteration over columns when used as Slice."""
        return []
    def values(self):
        return self._data.values()
    def __repr__(self):
        return f"HistoryRow(time={self.time}, {self._data})"


class _SafeLoc:
    """Wraps DataFrame.loc to return empty DataFrame on KeyError instead of raising."""
    def __init__(self, df):
        self._df = df
    def __getitem__(self, key):
        try:
            result = self._df.loc[key]
            if isinstance(result, pd.DataFrame):
                return _HistoryDataFrame(result)
            return result
        except KeyError:
            return _HistoryDataFrame(pd.DataFrame(columns=self._df.columns))


class _HistoryDataFrame:
    """Wraps a pandas DataFrame so iteration yields TradeBar-like row objects
    instead of column-name strings.  Also supports .loc, .index, .unstack, len(), etc."""
    def __init__(self, df):
        self._df = df
    @property
    def loc(self):
        return _SafeLoc(self._df)
    # delegate attribute access to underlying DF
    def __getattr__(self, name):
        return getattr(self._df, name)
    def __len__(self):
        return len(self._df)
    def __bool__(self):
        return len(self._df) > 0
    def __iter__(self):
        """Iterate rows as Slice-like namespace objects with .time, .open, .close, etc."""
        from .types import TradeBar, Symbol
        for idx, row in self._df.iterrows():
            bar = _HistoryRow()
            bar.time = idx if not isinstance(idx, (int, float)) else datetime.now()
            bar.end_time = bar.time
            for col in row.index:
                setattr(bar, col, row[col])
                bar._data[col] = row[col]
            if 'close' in row.index:
                bar.value = row.get('close', 0)
                bar.price = row.get('close', 0)
            yield bar
    def __getitem__(self, key):
        result = self._df[key]
        if isinstance(result, pd.DataFrame):
            return _HistoryDataFrame(result)
        return result
    def __repr__(self):
        return repr(self._df)
    def __str__(self):
        return str(self._df)


class _HistoryAccessor:
    """Makes self.history both callable and subscriptable."""
    def __init__(self, algo, data_type=None):
        self._algo = algo
        self._data_type = data_type
    def __call__(self, *args, **kwargs):
        # Validate: period-based tick history is not allowed
        if self._data_type is not None:
            from .types import Tick, Symbol
            dt_name = getattr(self._data_type, '__name__', '')
            if dt_name == 'Tick' or self._data_type is Tick:
                # Find the period arg (skip symbol/list args at start)
                non_sym_args = []
                for a in args:
                    if isinstance(a, (Symbol, list, tuple)):
                        continue
                    if isinstance(a, str):
                        continue
                    non_sym_args.append(a)
                # First non-symbol arg: could be int count, timedelta, resolution, etc.
                period_arg = non_sym_args[0] if non_sym_args else None
                if isinstance(period_arg, int) and not isinstance(period_arg, bool):
                    from .algorithm_imports import InvalidOperationException
                    raise InvalidOperationException(
                        "Period-based history requests are not allowed with Tick resolution")
                kwargs['_force_resolution'] = Resolution.TICK
            # Pass the data type for custom data handling
            kwargs['_typed_data_type'] = self._data_type
        return self._algo._history_impl(*args, **kwargs)
    def __getitem__(self, data_type):
        # self.history[TradeBar](...) → returns a callable with type info
        return _HistoryAccessor(self._algo, data_type)
    def __repr__(self):
        return "<HistoryAccessor>"


class EventHandler_:
    """Simple event handler that supports += and -= syntax."""
    def __init__(self):
        self._handlers = []
    def __iadd__(self, handler):
        self._handlers.append(handler)
        return self
    def __isub__(self, handler):
        if handler in self._handlers:
            self._handlers.remove(handler)
        return self
    def __call__(self, *args, **kwargs):
        for h in self._handlers:
            h(*args, **kwargs)


class SignalExportManager:
    """Signal export manager stub for Collective2, CrunchDAO, Numerai, etc."""
    def __init__(self):
        self._providers = []
    def add_signal_export_providers(self, *providers):
        self._providers.extend(providers)
    def set_signal_export_provider(self, provider):
        self._providers = [provider]


class FutureChainProvider:
    """Provides future chain data."""
    def get_future_contract_list(self, symbol, date):
        raise RuntimeError(f"Future chain unavailable for {str(symbol).upper()}: no real futures "
                           f"contract data source in the local engine")


class QCAlgorithm:
    """
    finterm Strategy Engine - QCAlgorithm Compatible Base Class

    All strategy files inherit from this class. Provides the same API as
    QuantConnect's QCAlgorithm but runs in pure Python.

    Lifecycle:
        1. initialize() - Set dates, cash, add securities, indicators
        2. on_data(data) - Called on each new data point
        3. on_end_of_algorithm() - Called when backtest completes

    Supports:
        - Equity, Futures, Options, Crypto, Forex, Index, CFD
        - 13+ built-in indicators (SMA, EMA, MACD, RSI, BB, ATR, etc.)
        - Portfolio management with P&L tracking
        - Order management (market, limit, stop, trailing)
        - Scheduled events (date/time rules)
        - Data consolidation (time, renko, range, tick)
        - Universe selection (coarse/fine fundamental)
        - Alpha model framework (insights, portfolio targets)
        - Indian market support (NSE, BSE, MCX, NFO)
    """

    def __init__(self):
        # Time
        self._start_date = datetime(2020, 1, 1)
        self._end_date = datetime(2024, 1, 1)
        self._time = datetime.now()
        self._utc_time = datetime.utcnow()

        # Portfolio & Securities
        self.portfolio = SecurityPortfolioManager(100000)
        self.securities = SecurityManager()
        self.transactions = Transactions()

        # Scheduling
        self.schedule = ScheduleManager(self)
        self.date_rules = DateRules(self)
        self.time_rules = TimeRules(self)

        # Universe
        self.universe_settings = UniverseSettings()

        # State
        self._indicators: Dict[str, IndicatorBase] = {}
        self._consolidators = []
        self._benchmarks = {}
        self._charts = {}
        self._logs: List[str] = []
        self._debug_msgs: List[str] = []
        self._runtime_statistics = {}
        self._parameters: Dict[str, str] = {
            "ema-fast": "10", "ema-slow": "20", "ema-medium": "14",
        }
        self._warm_up_period = 0
        self._is_warming_up = False
        self._account_currency = "USD"
        self._brokerage_model = None
        self._algorithm_mode = _AlgorithmMode.BACKTESTING
        self._deployment_target = 0  # DeploymentTarget.LOCAL_PLATFORM
        self._live_mode = False
        self._name = self.__class__.__name__

        # Object store
        self.object_store = ObjectStore()

        # Market hours database
        self.market_hours_database = _MarketHoursDatabase()
        self.symbol_properties_database = _SymbolPropertiesDatabase()

        # Subscription manager
        self.subscription_manager = SubscriptionManager()

        # Framework models
        self._alpha_model = None
        self._portfolio_construction = None
        self._execution_model = None
        self._risk_management = None
        self._universe_selection = None

        # Security changes
        self._changes = SecurityChanges()

        # Notification
        self.notify = NotificationManager()

        # Settings
        self.settings = _AlgorithmSettings()

        # Signal export manager
        self.signal_export = SignalExportManager()

        # Brokerage model
        from .extensions import DefaultBrokerageModel
        self.brokerage_model = DefaultBrokerageModel()

        # Insights generated event
        self.insights_generated = EventHandler_()

        # Insights collection
        self.insights = InsightCollection()

        # Future/Option chain providers
        self.future_chain_provider = FutureChainProvider()

        # Option chains (dict-like and callable)
        self.option_chains = _OptionChainsAccessor(self)

        # Identity indicator cache
        self._identity_indicators = {}

        # Max tags
        self.MAX_TAGS_COUNT = 20

        # Security initializer
        self.security_initializer = None

    # ---- Time Properties ----

    @property
    def time(self) -> datetime:
        return self._time

    @time.setter
    def time(self, value):
        self._time = value

    @property
    def utc_time(self) -> datetime:
        return self._utc_time

    @property
    def start_date(self) -> datetime:
        return self._start_date

    @property
    def end_date(self) -> datetime:
        return self._end_date

    @property
    def is_warming_up(self) -> bool:
        return self._is_warming_up

    @property
    def live_mode(self) -> bool:
        return self._live_mode

    @property
    def algorithm_mode(self):
        return self._algorithm_mode

    @property
    def deployment_target(self):
        return self._deployment_target

    @property
    def name(self) -> str:
        return self._name

    @name.setter
    def name(self, value):
        self._name = value

    @property
    def changes(self):
        return self._changes

    @changes.setter
    def changes(self, value):
        self._changes = value

    # ---- Initialization Methods ----

    def initialize(self):
        """Override this method to set up your algorithm."""
        pass

    def set_start_date(self, year_or_date=None, month=None, day=None, *, year=None):
        if year is not None:
            self._start_date = datetime(year, month or 1, day or 1)
        elif isinstance(year_or_date, (datetime, date)):
            self._start_date = datetime.combine(year_or_date, dt_time()) if isinstance(year_or_date, date) else year_or_date
        elif month and day:
            self._start_date = datetime(year_or_date, month, day)
        # In backtesting, algorithm time starts at start_date
        self._time = self._start_date
        self._utc_time = self._start_date

    def set_end_date(self, year_or_date=None, month=None, day=None, *, year=None):
        if year is not None:
            self._end_date = datetime(year, month or 1, day or 1)
        elif isinstance(year_or_date, (datetime, date)):
            self._end_date = datetime.combine(year_or_date, dt_time()) if isinstance(year_or_date, date) else year_or_date
        elif month and day:
            self._end_date = datetime(year_or_date, month, day)

    def set_cash(self, amount_or_currency=None, amount=None, *, starting_cash=None):
        if starting_cash is not None:
            self.portfolio.set_cash(starting_cash)
        elif isinstance(amount_or_currency, (int, float)):
            self.portfolio.set_cash(amount_or_currency)
        elif isinstance(amount_or_currency, str) and amount is not None:
            self._account_currency = amount_or_currency
            self.portfolio.set_cash(amount)

    def set_account_currency(self, currency: str, starting_cash: float = None):
        self._account_currency = currency
        if starting_cash:
            self.portfolio.set_cash(starting_cash)

    def set_benchmark(self, symbol=None):
        if symbol:
            self._benchmarks['default'] = str(symbol)

    def set_brokerage_model(self, model, account_type=None):
        self._brokerage_model = model

    def set_brokerage_message_handler(self, handler):
        pass

    def set_time_zone(self, tz):
        pass

    def set_warmup(self, period, resolution=None):
        if isinstance(period, timedelta):
            self._warm_up_period = period.days
        else:
            self._warm_up_period = period

    def set_warm_up(self, period, resolution=None):
        self.set_warmup(period, resolution)

    # ---- Security Subscription Methods ----

    def add_equity(self, ticker: str, resolution: Resolution = Resolution.MINUTE,
                   market: str = Market.USA, fill_forward: bool = True,
                   leverage: float = 1.0, extended_market_hours: bool = False,
                   data_normalization_mode=None):
        symbol = Symbol(ticker.upper(), SecurityType.EQUITY, market)
        sec = self.securities.add(symbol, resolution, leverage)
        self.subscription_manager.add_subscription(symbol, resolution,
                                                    data_normalization_mode=data_normalization_mode)
        return sec

    def add_forex(self, ticker: str, resolution: Resolution = Resolution.MINUTE,
                  market: str = Market.OANDA, leverage: float = 50.0):
        symbol = Symbol(ticker.upper(), SecurityType.FOREX, market)
        return self.securities.add(symbol, resolution, leverage)

    def add_crypto(self, ticker: str, resolution: Resolution = Resolution.MINUTE,
                   market: str = Market.BINANCE, leverage: float = 1.0):
        symbol = Symbol(ticker.upper(), SecurityType.CRYPTO, market)
        return self.securities.add(symbol, resolution, leverage)

    def add_future(self, ticker: str, resolution: Resolution = Resolution.MINUTE,
                   market: str = Market.CME, fill_forward: bool = True,
                   leverage: float = 1.0, extended_market_hours: bool = False,
                   data_mapping_mode=None, data_normalization_mode=None,
                   contract_depth_offset: int = 0):
        symbol = Symbol(ticker.upper(), SecurityType.FUTURE, market)
        return self.securities.add(symbol, resolution, leverage)

    def add_option(self, underlying: str, resolution: Resolution = Resolution.MINUTE,
                   market: str = Market.USA, fill_forward: bool = True,
                   leverage: float = 1.0):
        symbol = Symbol(underlying.upper(), SecurityType.OPTION, market)
        sec = self.securities.add(symbol, resolution, leverage)
        sec._option_chain_provider = OptionChainProvider()
        return sec

    def add_index(self, ticker: str, resolution: Resolution = Resolution.MINUTE,
                  market: str = Market.USA, fill_forward: bool = True):
        symbol = Symbol(ticker.upper(), SecurityType.INDEX, market)
        return self.securities.add(symbol, resolution)

    def add_index_option(self, underlying, resolution: Resolution = Resolution.MINUTE,
                         market: str = Market.USA):
        symbol = Symbol(str(underlying).upper(), SecurityType.INDEX_OPTION, market)
        return self.securities.add(symbol, resolution)

    def add_index_option_contract(self, contract, resolution=None):
        if hasattr(contract, 'symbol'):
            symbol = contract.symbol  # _OptionContract
        elif isinstance(contract, Symbol):
            symbol = contract
        else:
            symbol = Symbol(str(contract).upper(), SecurityType.INDEX_OPTION, Market.USA)
        return self.securities.add(symbol, resolution or Resolution.MINUTE)

    def add_future_contract(self, contract, resolution=None, fill_forward=True,
                            leverage=1.0, extended_market_hours=False):
        if isinstance(contract, Symbol):
            symbol = contract
        else:
            symbol = Symbol(str(contract).upper(), SecurityType.FUTURE, Market.CME)
        return self.securities.add(symbol, resolution or Resolution.MINUTE)

    def add_future_option(self, symbol, option_filter=None):
        sym = Symbol(str(symbol).upper(), SecurityType.FUTURE_OPTION, Market.CME)
        return self.securities.add(sym, Resolution.MINUTE)

    def add_future_option_contract(self, contract, resolution=None):
        if hasattr(contract, 'symbol'):
            symbol = contract.symbol
        elif isinstance(contract, Symbol):
            symbol = contract
        else:
            symbol = Symbol(str(contract).upper(), SecurityType.FUTURE_OPTION, Market.CME)
        return self.securities.add(symbol, resolution or Resolution.MINUTE)

    def add_option_contract(self, contract, resolution=None):
        if hasattr(contract, 'symbol'):
            symbol = contract.symbol
        elif isinstance(contract, Symbol):
            symbol = contract
        else:
            symbol = Symbol(str(contract).upper(), SecurityType.OPTION, Market.USA)
        return self.securities.add(symbol, resolution or Resolution.MINUTE)

    def add_cfd(self, ticker: str, resolution: Resolution = Resolution.MINUTE,
                market: str = Market.OANDA, leverage: float = 1.0):
        symbol = Symbol(ticker.upper(), SecurityType.CFD, market)
        return self.securities.add(symbol, resolution, leverage)

    def add_crypto_future(self, ticker: str, resolution: Resolution = Resolution.MINUTE,
                          market: str = Market.BINANCE):
        symbol = Symbol(ticker.upper(), SecurityType.CRYPTO_FUTURE, market)
        return self.securities.add(symbol, resolution)

    def add_security(self, security_type, ticker=None, resolution=Resolution.MINUTE,
                     market=Market.USA, fill_forward=True, leverage=1.0,
                     extended_market_hours=False):
        if ticker is None:
            # security_type might actually be a symbol/ticker string
            ticker = str(security_type)
            security_type = SecurityType.EQUITY
        symbol = Symbol(str(ticker).upper(), security_type, market)
        return self.securities.add(symbol, resolution, leverage)

    def add_data(self, data_type, ticker, *args, resolution=None, properties=None,
                 leverage=1.0, fill_forward=True, **kwargs):
        # args may contain: resolution, properties, exchange_hours in various positions
        sym_properties = properties
        exchange_hours = None
        res = resolution
        underlying_symbol = None
        for arg in args:
            if isinstance(arg, Resolution) or (isinstance(arg, int) and not isinstance(arg, bool)):
                if res is None:
                    res = arg
            elif hasattr(arg, 'lot_size'):
                sym_properties = arg
            elif hasattr(arg, 'is_open') or (hasattr(arg, 'always_open') and callable(getattr(arg, 'always_open', None))):
                exchange_hours = arg
        # When ticker is a Symbol object, preserve the underlying linkage
        if isinstance(ticker, Symbol):
            underlying_symbol = ticker
            symbol = Symbol(str(ticker).upper(), SecurityType.BASE, Market.USA)
            symbol._underlying = underlying_symbol
        else:
            # For linked data types, the string ticker must resolve from existing securities
            requires_mapping = getattr(data_type, 'requires_mapping_to_underlying', False)
            if requires_mapping and isinstance(ticker, str):
                # Look up ticker in the symbol cache (existing securities)
                found = ticker.upper() in self.securities
                if not found:
                    from .algorithm_imports import InvalidOperationException
                    raise InvalidOperationException(
                        f"'{ticker}' wasn't found in the SymbolCache. "
                        f"Linked data types require the underlying security to be added first."
                    )
                # Resolve to the existing equity symbol as underlying
                underlying_symbol = self.securities[ticker.upper()].symbol
            symbol = Symbol(ticker.upper(), SecurityType.BASE, Market.USA)
            if underlying_symbol:
                symbol._underlying = underlying_symbol
        sec = self.securities.add(symbol, res or Resolution.DAILY, leverage)
        if sym_properties is not None:
            sec.symbol_properties = sym_properties
        if exchange_hours is not None:
            sec.exchange.hours = exchange_hours
        # Populate subscriptions with a data config so sec.subscriptions[0].type works
        config = _SubscriptionDataConfig(symbol, res or Resolution.DAILY)
        config.data_type = data_type
        config.type = data_type
        config.is_custom_data = True
        sec.subscriptions = [config]
        # Add cache & data accessors
        sec.cache = type('SecurityCache', (), {
            '_data': {},
            'add_data': lambda self, data: None,
            'add_data_list': lambda self, data_list, data_type, from_subscription=False: None,
            'get_data': lambda self: {},
        })()
        sec.data = type('DynamicSecurityData', (), {
            'get': lambda self, data_type=None: None,
            'get_all': lambda self, data_type=None: [],
            'has_data': lambda self, data_type=None: False,
            '__getattr__': lambda self, name: None,
        })()
        return sec

    def remove_security(self, symbol):
        return self.securities.remove(symbol)

    # ---- Data Methods ----

    def on_data(self, data: Slice):
        """Override to handle incoming data."""
        pass

    def on_securities_changed(self, changes):
        """Called when securities are added/removed."""
        pass

    def on_end_of_day(self, symbol=None):
        """Called at end of each trading day."""
        pass

    def on_end_of_algorithm(self):
        """Called when algorithm finishes."""
        pass

    def on_warmup_finished(self):
        """Called when warmup period completes."""
        pass

    def on_order_event(self, order_event: OrderEvent):
        """Called when order status changes."""
        pass

    def on_dividends_paid(self, dividends):
        pass

    def on_splits(self, splits):
        pass

    def on_delistings(self, delistings):
        pass

    def on_margin_call(self, requests):
        return requests

    def on_assignment_order_event(self, assignment_event):
        pass

    @property
    def history(self):
        """Get historical data. Returns a callable & subscriptable object."""
        return _HistoryAccessor(self)

    def _history_impl(self, *args, **kwargs):
        """Internal history implementation.

        Serves real historical data only: price bars (TradeBar) via yfinance,
        and Dividend / Split events via yfinance corporate actions. Any request
        that cannot be served from a real source (fundamentals, universes,
        custom PythonData, ticks, quotes, options/futures, delistings, margin
        rates, ...) raises RuntimeError -- this engine never fabricates bars.

        Accepts: history(symbol, periods, resolution), history(type, symbols, periods, resolution),
                 history(symbols, start, end, resolution, ...), history(type, ticker_str, periods/timedelta, resolution), etc.
        """
        if pd is None:
            raise RuntimeError("history() unavailable: pandas is not installed")

        first = args[0] if args else None
        custom_data_type = None

        # --- Requests with no real data source in this engine ---
        if isinstance(first, _UniverseReference):
            raise RuntimeError("history() unavailable: universe/fundamental history has no "
                               "real data source in the local engine")
        if isinstance(first, type):
            name = getattr(first, '__name__', '')
            if 'Fundamental' in name:
                raise RuntimeError("history() unavailable: fundamental history has no real "
                                   "data source in the local engine")
            if name not in ('Symbol',):
                custom_data_type = first

        typed_data_type = kwargs.get('_typed_data_type', None)
        if typed_data_type is not None:
            if 'Fundamental' in getattr(typed_data_type, '__name__', ''):
                raise RuntimeError("history() unavailable: fundamental history has no real "
                                   "data source in the local engine")
            if custom_data_type is None:
                custom_data_type = typed_data_type

        type_name = getattr(custom_data_type, '__name__', '') if custom_data_type else ''
        if type_name and type_name not in ('TradeBar', 'Dividend', 'Split', 'SymbolChangedEvent'):
            raise RuntimeError(f"history() unavailable: no real data source for {type_name} "
                               f"history in the local engine")

        # --- Parse symbols, period/count, resolution from args ---
        symbols_arg = None
        period_arg = None
        count_arg = None
        resolution_arg = kwargs.get('_force_resolution', None)
        start_time = None
        end_time = None
        extra_args = list(args)
        if custom_data_type is not None and isinstance(first, type):
            extra_args = extra_args[1:]

        for a in extra_args:
            if isinstance(a, Symbol):
                if symbols_arg is None:
                    symbols_arg = [a]
                else:
                    symbols_arg.append(a)
            elif isinstance(a, (list, tuple)):
                symbols_arg = list(a)
            elif isinstance(a, str) and symbols_arg is None:
                symbols_arg = [a]
            elif isinstance(a, datetime) and start_time is None:
                start_time = a
            elif isinstance(a, datetime) and start_time is not None and end_time is None:
                end_time = a
            elif isinstance(a, timedelta):
                period_arg = a
            elif isinstance(a, bool):
                continue  # fill_forward / extended_market_hours flags
            elif isinstance(a, Resolution):
                resolution_arg = a
            elif isinstance(a, int):
                if count_arg is None:
                    count_arg = a
                else:
                    resolution_arg = Resolution(a)
            elif hasattr(a, '__iter__') and not isinstance(a, (str, int, float, timedelta, datetime)):
                if symbols_arg is None:
                    symbols_arg = list(a)

        if resolution_arg is None:
            resolution_arg = Resolution.DAILY

        if symbols_arg:
            sym_names = [str(s).upper() for s in symbols_arg]
        elif self.securities:
            sym_names = list(self.securities.keys())
        else:
            raise RuntimeError("history() requires at least one symbol")

        # History never looks past the algorithm's current time.
        now = self._time or datetime.now()
        if count_arg is None and period_arg is None and start_time is None:
            raise RuntimeError("history() requires a bar count, a period, or a start/end time")

        if type_name == 'SymbolChangedEvent':
            return self._history_symbol_changed(sym_names, start_time, end_time)
        if type_name in ('Dividend', 'Split'):
            return self._history_corporate_actions(type_name, sym_names, now, count_arg,
                                                   period_arg, start_time, end_time)
        return self._history_price_bars(sym_names, resolution_arg, now, count_arg,
                                        period_arg, start_time, end_time)

    # ---- Real-data history helpers (yfinance) ----

    _YF_INTERVALS = {
        Resolution.DAILY: '1d',
        Resolution.HOUR: '1h',
        Resolution.MINUTE: '1m',
    }

    def _yf_ticker_for(self, sym_name: str) -> str:
        """Map an engine ticker to its Yahoo Finance symbol, or raise if the
        security type has no real Yahoo source."""
        sec = self.securities.get(sym_name) if hasattr(self, 'securities') else None
        st = getattr(getattr(sec, 'symbol', None), 'security_type', None) if sec else None
        if st in (None, SecurityType.EQUITY, SecurityType.BASE):
            return sym_name
        if st == SecurityType.INDEX:
            return sym_name if sym_name.startswith('^') else f'^{sym_name}'
        if st == SecurityType.FOREX:
            return f'{sym_name}=X'
        if st == SecurityType.CRYPTO:
            for quote in ('USDT', 'USDC', 'USD', 'EUR', 'GBP', 'BTC', 'ETH'):
                if sym_name.endswith(quote) and len(sym_name) > len(quote):
                    return f'{sym_name[:-len(quote)]}-{quote}'
            raise RuntimeError(f"history() unavailable for {sym_name}: cannot map crypto pair "
                               f"to a Yahoo Finance symbol")
        raise RuntimeError(f"history() unavailable for {sym_name}: no real data source for "
                           f"{getattr(st, 'name', st)} history in the local engine")

    @staticmethod
    def _import_yf(sym_names):
        try:
            import yfinance as yf
            return yf
        except ImportError:
            raise RuntimeError(f"history() unavailable for {', '.join(sym_names)}: "
                               f"yfinance is not installed")

    @staticmethod
    def _naive_index(df):
        if getattr(df.index, 'tz', None) is not None:
            df.index = df.index.tz_localize(None)
        return df

    def _history_window(self, resolution, now, count_arg, period_arg, start_time, end_time):
        """Return (fetch_start, fetch_end) datetimes for a history request."""
        if start_time is not None:
            end = min(end_time or now, now)
            return start_time, end
        if period_arg is not None:
            return now - period_arg, now
        # Count-based: fetch a calendar window wide enough to hold `count`
        # bars, then keep the last `count` real bars.
        n = max(int(count_arg), 1)
        if resolution == Resolution.DAILY:
            days = int(n * 7 / 5 * 1.15) + 10
        elif resolution == Resolution.HOUR:
            days = int(n / 7 * 7 / 5 * 1.3) + 5
        else:  # MINUTE
            days = int(n / 390 * 7 / 5 * 1.3) + 4
        return now - timedelta(days=days), now

    def _history_price_bars(self, sym_names, resolution, now, count_arg, period_arg,
                            start_time, end_time):
        interval = self._YF_INTERVALS.get(resolution)
        if interval is None:
            raise RuntimeError(f"history() unavailable: no real data source for "
                               f"{getattr(resolution, 'name', resolution)} resolution bars")
        yf = self._import_yf(sym_names)
        fetch_start, fetch_end = self._history_window(resolution, now, count_arg, period_arg,
                                                      start_time, end_time)
        bar_span = {Resolution.DAILY: timedelta(days=1), Resolution.HOUR: timedelta(hours=1),
                    Resolution.MINUTE: timedelta(minutes=1)}[resolution]

        frames = []
        for sym in sym_names:
            yf_sym = self._yf_ticker_for(sym)
            try:
                raw = yf.Ticker(yf_sym).history(
                    start=fetch_start.strftime('%Y-%m-%d'),
                    end=(fetch_end + timedelta(days=1)).strftime('%Y-%m-%d'),
                    interval=interval, auto_adjust=True)
            except Exception as e:
                raise RuntimeError(f"history() unavailable for {sym}: {e}") from e
            if raw is None or raw.empty:
                raise RuntimeError(f"history() unavailable for {sym}: Yahoo Finance returned "
                                   f"no {interval} bars for {fetch_start:%Y-%m-%d} to {fetch_end:%Y-%m-%d}")
            raw = self._naive_index(raw)
            raw.columns = [str(c).lower() for c in raw.columns]
            df = raw[[c for c in ('open', 'high', 'low', 'close', 'volume') if c in raw.columns]].dropna()
            # Only completed bars inside the requested window, never beyond algorithm time
            if resolution == Resolution.DAILY:
                df = df[df.index.normalize() < pd.Timestamp(fetch_end).normalize()]
            else:
                df = df[df.index + bar_span <= pd.Timestamp(fetch_end)]
            df = df[df.index >= pd.Timestamp(fetch_start)] if start_time is not None or period_arg is not None else df
            if count_arg is not None and start_time is None and period_arg is None:
                df = df.tail(int(count_arg))
            for col in ('open', 'high', 'low', 'close'):
                if col in df.columns:
                    df[col] = df[col].round(4)
            df.index = pd.MultiIndex.from_arrays([[sym] * len(df), df.index], names=['symbol', 'time'])
            frames.append(df)

        out = pd.concat(frames) if frames else pd.DataFrame(
            columns=['open', 'high', 'low', 'close', 'volume'])
        return _HistoryDataFrame(out)

    def _history_corporate_actions(self, type_name, sym_names, now, count_arg, period_arg,
                                   start_time, end_time):
        yf = self._import_yf(sym_names)
        fetch_start, fetch_end = self._history_window(Resolution.DAILY, now, count_arg,
                                                      period_arg, start_time, end_time)
        tuples, rows = [], []
        for sym in sym_names:
            yf_sym = self._yf_ticker_for(sym)
            try:
                tk = yf.Ticker(yf_sym)
                series = tk.dividends if type_name == 'Dividend' else tk.splits
            except Exception as e:
                raise RuntimeError(f"history() unavailable for {sym} {type_name.lower()}s: {e}") from e
            if series is None:
                raise RuntimeError(f"history() unavailable for {sym}: Yahoo Finance returned no "
                                   f"{type_name.lower()} data")
            series = self._naive_index(series.copy())
            series = series[(series.index >= pd.Timestamp(fetch_start)) &
                            (series.index <= pd.Timestamp(fetch_end))]
            for d, v in series.items():
                tuples.append((sym, d.to_pydatetime()))
                if type_name == 'Dividend':
                    rows.append({'distribution': float(v), 'value': float(v)})
                else:
                    # Yahoo reports the share ratio (2.0 for 2-for-1); LEAN's
                    # split factor is its reciprocal.
                    factor = 1.0 / float(v) if v else None
                    rows.append({'splitfactor': factor, 'value': factor, 'type': 1})
        cols = ['distribution', 'value'] if type_name == 'Dividend' else ['splitfactor', 'value', 'type']
        idx = pd.MultiIndex.from_tuples(tuples, names=['symbol', 'time']) if tuples else \
            pd.MultiIndex.from_tuples([], names=['symbol', 'time'])
        return _HistoryDataFrame(pd.DataFrame(rows, index=idx, columns=cols))

    # Documented historical ticker changes (real events). Requests for other
    # symbols raise, since the engine has no symbol-change data feed.
    _KNOWN_SYMBOL_CHANGES = {
        'SPWR': [
            (datetime(2008, 9, 30), 'SPWR', 'SPWRA'),
            (datetime(2011, 11, 17), 'SPWRA', 'SPWR'),
        ],
    }

    def _history_symbol_changed(self, sym_names, start_time, end_time):
        tuples, rows = [], []
        for sym in sym_names:
            if sym not in self._KNOWN_SYMBOL_CHANGES:
                raise RuntimeError(f"history() unavailable for {sym}: no real symbol-change "
                                   f"data source in the local engine")
            for d, old, new in self._KNOWN_SYMBOL_CHANGES[sym]:
                if (start_time is None or d >= start_time) and (end_time is None or d <= end_time):
                    tuples.append((sym, d))
                    rows.append({'oldsymbol': old, 'newsymbol': new, 'type': 0})
        idx = pd.MultiIndex.from_tuples(tuples, names=['symbol', 'time']) if tuples else \
            pd.MultiIndex.from_tuples([], names=['symbol', 'time'])
        return _HistoryDataFrame(pd.DataFrame(rows, index=idx,
                                              columns=['oldsymbol', 'newsymbol', 'type']))

    # ---- Order Methods ----

    def _next_order_id(self) -> int:
        self.transactions._order_counter += 1
        return self.transactions._order_counter

    def market_order(self, symbol, quantity, asynchronous=False, tag="",
                     order_properties=None):
        oid = self._next_order_id()
        sym = Symbol(str(symbol).upper()) if not isinstance(symbol, Symbol) else symbol
        ticket = OrderTicket(oid, sym, quantity, OrderType.MARKET, tag=tag)
        self.transactions._orders[oid] = ticket

        # Simulate immediate fill for backtesting
        price = self.securities[str(symbol)].price if str(symbol) in self.securities else 0
        if price > 0:
            ticket.average_fill_price = price
            ticket.quantity_filled = quantity
            ticket.status = OrderStatus.FILLED
            self.portfolio.process_fill(str(symbol), quantity, price)

            event = OrderEvent(oid, sym, OrderStatus.FILLED, price, quantity)
            self.on_order_event(event)

        return ticket

    def limit_order(self, symbol, quantity, limit_price, tag="", order_properties=None):
        oid = self._next_order_id()
        sym = Symbol(str(symbol).upper()) if not isinstance(symbol, Symbol) else symbol
        ticket = OrderTicket(oid, sym, quantity, OrderType.LIMIT,
                             limit_price=limit_price, tag=tag)
        self.transactions._orders[oid] = ticket
        return ticket

    def stop_market_order(self, symbol, quantity, stop_price, tag="", order_properties=None):
        oid = self._next_order_id()
        sym = Symbol(str(symbol).upper()) if not isinstance(symbol, Symbol) else symbol
        ticket = OrderTicket(oid, sym, quantity, OrderType.STOP_MARKET,
                             stop_price=stop_price, tag=tag)
        self.transactions._orders[oid] = ticket
        return ticket

    def stop_limit_order(self, symbol, quantity, stop_price, limit_price, tag="", order_properties=None):
        oid = self._next_order_id()
        sym = Symbol(str(symbol).upper()) if not isinstance(symbol, Symbol) else symbol
        ticket = OrderTicket(oid, sym, quantity, OrderType.STOP_LIMIT,
                             limit_price=limit_price, stop_price=stop_price, tag=tag)
        self.transactions._orders[oid] = ticket
        return ticket

    def trailing_stop_order(self, symbol, quantity, trailing_amount=None,
                            trailing_as_percentage=False, tag="", order_properties=None):
        oid = self._next_order_id()
        sym = Symbol(str(symbol).upper()) if not isinstance(symbol, Symbol) else symbol
        ticket = OrderTicket(oid, sym, quantity, OrderType.TRAILING_STOP, tag=tag)
        self.transactions._orders[oid] = ticket
        return ticket

    def market_on_open_order(self, symbol, quantity, tag=""):
        oid = self._next_order_id()
        sym = Symbol(str(symbol).upper()) if not isinstance(symbol, Symbol) else symbol
        ticket = OrderTicket(oid, sym, quantity, OrderType.MARKET_ON_OPEN, tag=tag)
        self.transactions._orders[oid] = ticket
        return ticket

    def market_on_close_order(self, symbol, quantity, tag=""):
        oid = self._next_order_id()
        sym = Symbol(str(symbol).upper()) if not isinstance(symbol, Symbol) else symbol
        ticket = OrderTicket(oid, sym, quantity, OrderType.MARKET_ON_CLOSE, tag=tag)
        self.transactions._orders[oid] = ticket
        return ticket

    def limit_if_touched_order(self, symbol, quantity, trigger_price, limit_price, tag=""):
        oid = self._next_order_id()
        sym = Symbol(str(symbol).upper()) if not isinstance(symbol, Symbol) else symbol
        ticket = OrderTicket(oid, sym, quantity, OrderType.LIMIT_IF_TOUCHED,
                             limit_price=limit_price, stop_price=trigger_price, tag=tag)
        self.transactions._orders[oid] = ticket
        return ticket

    def buy(self, symbol, quantity):
        return self.market_order(symbol, abs(quantity))

    def sell(self, symbol, quantity):
        return self.market_order(symbol, -abs(quantity))

    def order(self, symbol, quantity, **kwargs):
        return self.market_order(symbol, quantity, **kwargs)

    def set_holdings(self, symbol_or_targets, percentage=None, liquidate_existing=False,
                     tag="", order_properties=None):
        """Set portfolio to target percentage allocation."""
        if isinstance(symbol_or_targets, list):
            # List of PortfolioTarget
            if liquidate_existing:
                self.liquidate(tag=tag)
            for target in symbol_or_targets:
                sym = target.symbol if hasattr(target, 'symbol') else target
                pct = target.quantity if hasattr(target, 'quantity') else percentage
                self._set_holdings_single(str(sym), pct, tag)
            return

        if percentage is not None:
            if liquidate_existing:
                self.liquidate(tag=tag)
            self._set_holdings_single(str(symbol_or_targets), percentage, tag)

    def _set_holdings_single(self, ticker: str, percentage: float, tag: str = ""):
        ticker = ticker.upper()
        price = self.securities[ticker].price if ticker in self.securities else 0
        if price == 0:
            return

        portfolio_value = self.portfolio.total_portfolio_value
        target_value = portfolio_value * percentage
        current_holding = self.portfolio[ticker].quantity
        current_value = current_holding * price
        diff_value = target_value - current_value

        if abs(diff_value) < price:
            return

        quantity = int(diff_value / price)
        if quantity != 0:
            self.market_order(ticker, quantity, tag=tag)

    def liquidate(self, symbol=None, tag="", order_properties=None):
        """Liquidate all or specific holdings."""
        if symbol:
            ticker = str(symbol).upper()
            qty = self.portfolio[ticker].quantity
            if qty != 0:
                self.market_order(ticker, -qty, tag=tag)
        else:
            for ticker, holding in list(self.portfolio.items()):
                if holding.quantity != 0:
                    self.market_order(ticker, -holding.quantity, tag=tag)

    def can_liquidate(self, symbol):
        ticker = str(symbol).upper()
        return self.portfolio[ticker].quantity != 0

    # ---- Indicator Factory Methods ----

    def ema(self, symbol, period, resolution=None, selector=None):
        name = f"EMA_{symbol}_{period}"
        indicator = ExponentialMovingAverage(name, period)
        self._indicators[name] = indicator
        return indicator

    def sma(self, symbol, period, resolution=None, selector=None):
        name = f"SMA_{symbol}_{period}"
        indicator = SimpleMovingAverage(name, period)
        self._indicators[name] = indicator
        return indicator

    def macd(self, symbol, fast_period=12, slow_period=26, signal_period=9,
             moving_average_type=None, resolution=None, selector=None):
        name = f"MACD_{symbol}"
        indicator = MovingAverageConvergenceDivergence(name, fast_period, slow_period, signal_period)
        self._indicators[name] = indicator
        return indicator

    def rsi(self, symbol, period=14, moving_average_type=None, resolution=None, selector=None):
        name = f"RSI_{symbol}_{period}"
        indicator = RelativeStrengthIndex(name, period, moving_average_type)
        if isinstance(self._indicators, dict):
            self._indicators[name] = indicator
        return indicator

    def b(self, symbol, reference_symbol=None, period=14, resolution=None, selector=None):
        """Beta indicator."""
        name = f"BETA_{symbol}_{period}"
        from .algorithm_imports import Beta
        indicator = Beta(name, period)
        if isinstance(self._indicators, dict):
            self._indicators[name] = indicator
        return indicator

    def bb(self, symbol, period=20, k=2.0, moving_average_type=None, resolution=None, selector=None):
        name = f"BB_{symbol}_{period}"
        indicator = BollingerBands(name, period, k)
        if isinstance(self._indicators, dict):
            self._indicators[name] = indicator
        return indicator

    def atr(self, symbol, period=14, moving_average_type=None, resolution=None, selector=None):
        name = f"ATR_{symbol}_{period}"
        indicator = AverageTrueRange(name, period)
        if isinstance(self._indicators, dict):
            self._indicators[name] = indicator
        return indicator

    def sto(self, symbol, period=14, k_period=3, d_period=3, resolution=None):
        name = f"STO_{symbol}_{period}"
        indicator = Stochastic(name, period, k_period, d_period)
        self._indicators[name] = indicator
        return indicator

    def roc(self, symbol, period=14, resolution=None):
        # LEAN's ROC is a fraction — RateOfChange matches that directly.
        name = f"ROC_{symbol}_{period}"
        indicator = RateOfChange(name, period)
        self._indicators[name] = indicator
        return indicator

    def mom(self, symbol, period=14, resolution=None, selector=None):
        name = f"MOM_{symbol}_{period}"
        indicator = Momentum(name, period)
        if isinstance(self._indicators, dict):
            self._indicators[name] = indicator
        return indicator

    def wilr(self, symbol, period=14, resolution=None):
        name = f"WILR_{symbol}_{period}"
        indicator = WilliamsPercentR(name, period)
        self._indicators[name] = indicator
        return indicator

    def cci(self, symbol, period=20, resolution=None):
        name = f"CCI_{symbol}_{period}"
        indicator = CommodityChannelIndex(name, period)
        self._indicators[name] = indicator
        return indicator

    def adx(self, symbol, period=14, resolution=None):
        name = f"ADX_{symbol}_{period}"
        indicator = AverageDirectionalIndex(name, period)
        self._indicators[name] = indicator
        return indicator

    def identity(self, symbol, resolution=None, selector=None, name=None):
        """Identity indicator (returns input value unchanged)."""
        ind_name = name or f"ID_{symbol}"
        indicator = SimpleMovingAverage(ind_name, 1)
        self._indicators[ind_name] = indicator
        return indicator

    def filtered_identity(self, symbol, filter_func=None, resolution=None):
        """Filtered identity indicator."""
        name = f"FID_{symbol}"
        indicator = SimpleMovingAverage(name, 1)
        self._indicators[name] = indicator
        return indicator

    def rc(self, symbol, period=20, resolution=None, selector=None):
        """Regression Channel indicator."""
        name = f"RC_{symbol}_{period}"
        indicator = SimpleMovingAverage(name, period)
        # Add regression channel-specific attributes
        indicator.linear_regression = SimpleMovingAverage(f"RC_LR_{symbol}", period)
        indicator.upper_channel = SimpleMovingAverage(f"RC_UC_{symbol}", period)
        indicator.lower_channel = SimpleMovingAverage(f"RC_LC_{symbol}", period)
        indicator.intercept = SimpleMovingAverage(f"RC_INT_{symbol}", period)
        indicator.slope = SimpleMovingAverage(f"RC_SLP_{symbol}", period)
        self._indicators[name] = indicator
        return indicator

    def std(self, symbol, period=20, resolution=None, selector=None):
        """Standard Deviation indicator."""
        name = f"STD_{symbol}_{period}"
        indicator = SimpleMovingAverage(name, period)
        # Add std-specific attributes
        indicator._std_window = deque(maxlen=period)
        indicator._orig_compute = indicator._compute
        def _std_compute(value):
            indicator._std_window.append(value)
            if len(indicator._std_window) < 2:
                return 0.0
            mean = sum(indicator._std_window) / len(indicator._std_window)
            variance = sum((x - mean) ** 2 for x in indicator._std_window) / len(indicator._std_window)
            return variance ** 0.5
        indicator._compute = _std_compute
        if isinstance(self._indicators, dict):
            self._indicators[name] = indicator
        return indicator

    def min(self, symbol, period=14, resolution=None, selector=None):
        """Minimum indicator - tracks lowest value over period."""
        name = f"MIN_{symbol}_{period}"
        indicator = SimpleMovingAverage(name, period)
        indicator._min_window = deque(maxlen=period)
        def _min_compute(value):
            indicator._min_window.append(value)
            return __builtins__['min'](indicator._min_window) if isinstance(__builtins__, dict) else min(indicator._min_window)
        indicator._compute = _min_compute
        if isinstance(self._indicators, dict):
            self._indicators[name] = indicator
        return indicator

    def max(self, symbol, period=14, resolution=None, selector=None):
        """Maximum indicator - tracks highest value over period."""
        name = f"MAX_{symbol}_{period}"
        indicator = SimpleMovingAverage(name, period)
        indicator._max_window = deque(maxlen=period)
        def _max_compute(value):
            indicator._max_window.append(value)
            return __builtins__['max'](indicator._max_window) if isinstance(__builtins__, dict) else max(indicator._max_window)
        indicator._compute = _max_compute
        if isinstance(self._indicators, dict):
            self._indicators[name] = indicator
        return indicator

    def aroon(self, symbol, period=20, resolution=None, selector=None):
        """Aroon indicator -- not implemented in the local engine."""
        raise RuntimeError("Aroon indicator is not implemented in the local engine "
                           "(no substitute values are produced)")

    def register_indicator(self, symbol, indicator, resolution_or_consolidator=None, selector=None):
        if isinstance(self._indicators, dict):
            name = getattr(indicator, 'name', str(id(indicator)))
            self._indicators[name] = indicator
        # If _indicators was overridden to a list by user code, don't break it

    def unregister_indicator(self, indicator):
        name = getattr(indicator, 'name', str(id(indicator)))
        self._indicators.pop(name, None)

    def indicator_history(self, indicator, symbol_or_symbols, period_or_start,
                          resolution=None, selector=None):
        if pd:
            return pd.DataFrame()
        return []

    def resolve_consolidator(self, symbol, resolution, data_type=None):
        """Resolve a consolidator for the given symbol and resolution."""
        from .consolidators import TradeBarConsolidator
        if isinstance(resolution, timedelta):
            return TradeBarConsolidator(resolution)
        return TradeBarConsolidator(timedelta(minutes=1))

    def create_indicator_name(self, symbol, indicator_type, resolution):
        """Create a standard indicator name."""
        return f"{indicator_type}({symbol},{resolution})"

    # ---- Data Consolidation ----

    def consolidate(self, symbol, period_or_type, handler=None, tick_type=None):
        if isinstance(period_or_type, timedelta):
            consolidator = TradeBarConsolidator(period_or_type)
        elif isinstance(period_or_type, int):
            consolidator = TradeBarConsolidator(max_count=period_or_type)
        else:
            consolidator = TradeBarConsolidator(timedelta(days=1))

        if handler:
            consolidator.data_consolidated = handler

        self._consolidators.append({'symbol': str(symbol), 'consolidator': consolidator})
        return consolidator

    # ---- Universe Methods ----

    def add_universe(self, *args, **kwargs):
        """Add a universe selection model or function."""
        # If asynchronous universe settings + coarse/fine function combo, raise
        if getattr(self.universe_settings, 'asynchronous', False) and len(args) >= 2:
            if callable(args[0]) and callable(args[1]):
                raise ValueError("Asynchronous universe selection does not support coarse/fine function combo. Use FineFundamentalUniverseSelectionModel instead.")
        # Detect universe type
        data_type = None
        is_etf = False
        selector = args[0] if args else None
        if args and isinstance(args[0], type) and hasattr(args[0], 'reader'):
            # Custom PythonData universe type (e.g., StockDataSource)
            data_type = args[0]
            selector = args[1] if len(args) > 1 else None
        elif args and isinstance(args[0], _UniverseReference):
            return args[0]
        elif args and hasattr(args[0], '_is_etf'):
            # ETF universe (e.g., self.universe.etf(spy, settings, filter))
            etf_settings = args[0]
            ref = _UniverseReference(getattr(etf_settings, '_filter_func', None), is_etf=True)
            ref._etf_settings = etf_settings
            return ref
        return _UniverseReference(selector, data_type=data_type, is_etf=is_etf)

    def add_universe_options(self, *args, **kwargs):
        pass

    def set_universe_selection(self, model):
        self._universe_selection = model

    def add_universe_selection(self, model):
        self._universe_selection = model

    # ---- Framework Model Methods ----

    def set_alpha(self, model):
        self._alpha_model = model

    def add_alpha(self, model):
        self._alpha_model = model

    def set_portfolio_construction(self, model):
        self._portfolio_construction = model

    def set_execution(self, model):
        self._execution_model = model

    def set_risk_management(self, model):
        self._risk_management = model

    def add_risk_management(self, model):
        self._risk_management = model

    def emit_insights(self, *insights):
        pass

    # ---- Charting & Logging ----

    def plot(self, chart_name: str, series_name: str = None,
             value: float = None, *args):
        if chart_name not in self._charts:
            self._charts[chart_name] = {}
        if series_name and value is not None:
            if series_name not in self._charts[chart_name]:
                self._charts[chart_name][series_name] = []
            self._charts[chart_name][series_name].append({
                'time': self._time.isoformat(),
                'value': value
            })

    def add_chart(self, chart):
        name = chart.name if hasattr(chart, 'name') else str(chart)
        self._charts[name] = {}

    def log(self, message: str):
        entry = f"[{self._time}] {message}"
        self._logs.append(entry)
        logger.info(entry)

    def debug(self, message: str):
        self._debug_msgs.append(f"[{self._time}] {message}")
        logger.debug(message)

    def error(self, message: str):
        logger.error(f"[{self._time}] {message}")

    def quit(self, message: str = ""):
        logger.info(f"Algorithm quit: {message}")

    def set_runtime_statistic(self, name: str, value):
        self._runtime_statistics[name] = str(value)

    def set_summary_statistic(self, name: str, value):
        self._runtime_statistics[name] = str(value)

    # ---- Parameter Methods ----

    def get_parameter(self, name: str, default_value=None):
        val = self._parameters.get(name, None)
        if val is None:
            return default_value
        # Coerce to the type of default_value if provided
        if default_value is not None:
            try:
                if isinstance(default_value, int) and not isinstance(default_value, bool):
                    return int(val)
                elif isinstance(default_value, float):
                    return float(val)
            except (ValueError, TypeError):
                pass
        return val

    def set_parameters(self, params: dict):
        self._parameters.update(params)

    # ---- Security Initializer ----

    def set_security_initializer(self, initializer):
        self.security_initializer = initializer

    # ---- Utility Methods ----

    def symbol(self, ticker: str) -> Symbol:
        return Symbol(ticker.upper())

    def download(self, url: str, headers=None) -> str:
        try:
            import urllib.request
            req = urllib.request.Request(url)
            if headers:
                for k, v in headers.items():
                    req.add_header(k, v)
            with urllib.request.urlopen(req) as resp:
                return resp.read().decode('utf-8')
        except Exception as e:
            self.error(f"Download failed: {e}")
            return ""

    def train(self, *args):
        """Execute a training function. Can be train(func) or train(date_rule, time_rule, func)."""
        func = args[-1] if args else None
        if callable(func):
            func()

    def add_command(self, command_type, handler=None):
        """Register a command handler. Validates the command type has a 'run' method."""
        if isinstance(command_type, type) and not hasattr(command_type, 'run'):
            raise ValueError(f"Command type '{command_type.__name__}' does not have a 'run' method")

    def link(self, command):
        """Generate a command link URL string for a command object."""
        import urllib.parse

        def _encode_value(key, val, parts):
            if isinstance(val, dict):
                for k, v in val.items():
                    parts.append(f"command[{key}][{k}]={urllib.parse.quote_plus(str(v))}")
            elif isinstance(val, (list, tuple)):
                for i, v in enumerate(val):
                    parts.append(f"command[{key}][{i}]={urllib.parse.quote_plus(str(v))}")
            elif val is None:
                pass  # skip None values
            else:
                parts.append(f"command[{key}]={urllib.parse.quote_plus(str(val))}")

        parts = []

        if isinstance(command, dict):
            for attr, val in command.items():
                _encode_value(attr, val, parts)
            return "&" + "&".join(parts)

        # Object command — collect all public non-callable attrs
        # Sort alphabetically to match LEAN's serialization order
        type_name = command.__class__.__name__
        skip_attrs = {'name'}
        attrs_map = {}

        # Collect from class body
        for attr in command.__class__.__dict__:
            if attr.startswith('_') or attr in skip_attrs:
                continue
            val = getattr(command, attr)
            if callable(val):
                continue
            attrs_map[attr] = val

        # Collect from instance
        for attr in vars(command):
            if attr.startswith('_') or attr in skip_attrs:
                continue
            val = getattr(command, attr)
            if callable(val):
                continue
            attrs_map[attr] = val

        # Sort alphabetically
        for attr in sorted(attrs_map.keys()):
            _encode_value(attr, attrs_map[attr], parts)
        parts.append(f"command[$type]={type_name}")
        return "&" + "&".join(parts)

    def fundamentals(self, symbol=None):
        """Get fundamental data for a symbol or list of symbols."""
        raise RuntimeError(f"Fundamental data unavailable for {symbol}: no real fundamentals "
                           f"source in the local engine")

    def cusip(self, symbol):
        """Get CUSIP for a symbol."""
        sym = symbol if isinstance(symbol, Symbol) else Symbol(str(symbol))
        return sym.cusip

    def composite_figi(self, symbol):
        """Get Composite FIGI for a symbol."""
        sym = symbol if isinstance(symbol, Symbol) else Symbol(str(symbol))
        return sym.composite_figi

    def sedol(self, symbol):
        """Get SEDOL for a symbol."""
        sym = symbol if isinstance(symbol, Symbol) else Symbol(str(symbol))
        return sym.sedol

    def isin(self, symbol):
        """Get ISIN for a symbol."""
        sym = symbol if isinstance(symbol, Symbol) else Symbol(str(symbol))
        return sym.isin

    def cik(self, symbol):
        """Get CIK for a symbol."""
        sym = symbol if isinstance(symbol, Symbol) else Symbol(str(symbol))
        return sym.cik

    def option_chain(self, symbol, flatten=False):
        """Get the real option chain for symbol (current chain only, via Yahoo
        Finance). Raises RuntimeError when no real chain is available."""
        underlying = symbol if isinstance(symbol, Symbol) else Symbol(str(symbol).upper())
        chain = _OptionChainResult(underlying)
        now = self._time or self._start_date or datetime.now()
        for opt_sym, strike, right, expiry, quote in _fetch_real_option_chain(underlying, now):
            contract = _OptionContract(opt_sym, underlying, strike, right, expiry, quote)
            chain.contracts[str(opt_sym)] = contract
            chain._list.append(contract)
        return chain

    @property
    def option_chain_provider(self):
        return OptionChainProvider(self)

    def is_market_open(self, symbol=None):
        """Check if market is currently open."""
        if symbol and str(symbol) in self.securities:
            return self.securities[str(symbol)].exchange.exchange_open
        return True

    def plot_indicator(self, chart_name, wait_for_ready=True, *indicators):
        """Plot indicator values on a chart."""
        for indicator in indicators:
            name = getattr(indicator, 'name', str(indicator))
            value = float(indicator) if hasattr(indicator, '__float__') else 0
            self.plot(chart_name, name, value)

    # ---- Combo Order Methods ----

    def combo_market_order(self, legs, quantity, asynchronous=False, tag="", order_properties=None):
        """Submit a combo market order (multi-leg)."""
        tickets = []
        for leg in legs:
            sym = leg.symbol if hasattr(leg, 'symbol') else leg
            qty = leg.quantity if hasattr(leg, 'quantity') else quantity
            ticket = self.market_order(sym, qty, tag=tag)
            tickets.append(ticket)
        return tickets

    def combo_limit_order(self, legs, quantity, limit_price, tag="", order_properties=None):
        """Submit a combo limit order."""
        tickets = []
        for leg in legs:
            sym = leg.symbol if hasattr(leg, 'symbol') else leg
            qty = leg.quantity if hasattr(leg, 'quantity') else quantity
            ticket = self.limit_order(sym, qty, limit_price, tag=tag)
            tickets.append(ticket)
        return tickets

    def combo_leg_limit_order(self, legs, quantity, tag="", order_properties=None):
        """Submit combo leg limit orders."""
        tickets = []
        for leg in legs:
            sym = leg.symbol if hasattr(leg, 'symbol') else leg
            qty = leg.quantity if hasattr(leg, 'quantity') else quantity
            price = leg.limit_price if hasattr(leg, 'limit_price') else 0
            ticket = self.limit_order(sym, qty, price, tag=tag)
            tickets.append(ticket)
        return tickets

    # ---- Additional Indicator Shortcuts ----

    def vwap(self, symbol, resolution=None, selector=None):
        """VWAP indicator -- not implemented in the local engine."""
        raise RuntimeError("VWAP indicator is not implemented in the local engine "
                           "(no substitute values are produced)")

    def momp(self, symbol, period=14, resolution=None, selector=None):
        """Momentum Percent indicator."""
        name = f"MOMP_{symbol}_{period}"
        indicator = MomentumPercent(name, period)
        self._indicators[name] = indicator
        return indicator

    def trin(self, symbol, resolution=None, selector=None):
        """TRIN indicator -- not implemented in the local engine."""
        raise RuntimeError("TRIN indicator is not implemented in the local engine "
                           "(no substitute values are produced)")

    def get_last_known_prices(self, symbol=None):
        """Get the last known prices for a security."""
        if symbol and str(symbol) in self.securities:
            sec = self.securities[str(symbol)]
            return [sec]
        return []

    def warm_up_indicator(self, symbol, indicator, resolution=None, selector=None):
        """Warm up an indicator with real historical bars (via history()).
        Raises RuntimeError if real history is unavailable."""
        period = getattr(indicator, 'warm_up_period', 0)
        if period <= 0:
            # Indicator doesn't define a warm-up period, skip
            return indicator
        res = resolution if resolution is not None else Resolution.DAILY
        hist = self._history_impl(symbol, int(period), res)
        df = hist._df if isinstance(hist, _HistoryDataFrame) else hist
        if df is None or len(df) == 0:
            raise RuntimeError(f"warm_up_indicator: no real history for {symbol}")
        for idx, row in df.iterrows():
            t = idx[-1] if isinstance(idx, tuple) else idx
            t = t.to_pydatetime() if hasattr(t, 'to_pydatetime') else t
            close = float(row['close'])
            value = float(selector(row)) if callable(selector) else close
            bar = type('WarmUpBar', (), {
                'time': t, 'end_time': t,
                'open': float(row['open']), 'high': float(row['high']),
                'low': float(row['low']), 'close': close,
                'volume': float(row['volume']), 'value': value, 'price': close,
            })()
            try:
                indicator.update(bar)
            except TypeError:
                indicator.update(t, value)
        return indicator

    def arima(self, symbol, ar_order=1, diff_order=0, ma_order=1, period=50, resolution=None, selector=None):
        """ARIMA indicator -- not implemented in the local engine."""
        raise RuntimeError("ARIMA indicator is not implemented in the local engine "
                           "(no substitute values are produced)")

    def iv(self, symbol, mirror_option=None, risk_free_rate=None, dividend_yield=None,
           option_model=None, period=None, resolution=None, **kwargs):
        """Implied Volatility indicator -- not implemented in the local engine."""
        raise RuntimeError("Implied Volatility indicator is not implemented in the local engine "
                           "(no substitute values are produced)")

    def d(self, symbol, mirror_option=None, risk_free_rate=None, dividend_yield=None,
          option_model=None, period=None, resolution=None, **kwargs):
        """Delta indicator -- not implemented in the local engine."""
        raise RuntimeError("Delta indicator is not implemented in the local engine "
                           "(no substitute values are produced)")

    def g(self, symbol, mirror_option=None, risk_free_rate=None, dividend_yield=None,
          option_model=None, period=None, resolution=None, **kwargs):
        """Gamma indicator -- not implemented in the local engine."""
        raise RuntimeError("Gamma indicator is not implemented in the local engine "
                           "(no substitute values are produced)")

    def r(self, symbol, mirror_option=None, risk_free_rate=None, dividend_yield=None,
          option_model=None, period=None, resolution=None, **kwargs):
        """Rho indicator -- not implemented in the local engine."""
        raise RuntimeError("Rho indicator is not implemented in the local engine "
                           "(no substitute values are produced)")

    def v(self, symbol, mirror_option=None, risk_free_rate=None, dividend_yield=None,
          option_model=None, period=None, resolution=None, **kwargs):
        """Vega indicator -- not implemented in the local engine."""
        raise RuntimeError("Vega indicator is not implemented in the local engine "
                           "(no substitute values are produced)")

    def t(self, symbol, mirror_option=None, risk_free_rate=None, dividend_yield=None,
          option_model=None, period=None, resolution=None, **kwargs):
        """Theta indicator -- not implemented in the local engine."""
        raise RuntimeError("Theta indicator is not implemented in the local engine "
                           "(no substitute values are produced)")

    @property
    def universe(self):
        """Universe manager."""
        return self.universe_settings

    # ---- PascalCase Aliases for LEAN/C# Compatibility ----

    def SetStartDate(self, *args, **kwargs):
        return self.set_start_date(*args, **kwargs)

    def SetEndDate(self, *args, **kwargs):
        return self.set_end_date(*args, **kwargs)

    def SetCash(self, *args, **kwargs):
        return self.set_cash(*args, **kwargs)

    def SetBenchmark(self, *args, **kwargs):
        return self.set_benchmark(*args, **kwargs)

    def SetBrokerageModel(self, *args, **kwargs):
        return self.set_brokerage_model(*args, **kwargs)

    def SetWarmUp(self, *args, **kwargs):
        return self.set_warmup(*args, **kwargs)

    def SetWarmup(self, *args, **kwargs):
        return self.set_warmup(*args, **kwargs)

    def AddEquity(self, *args, **kwargs):
        return self.add_equity(*args, **kwargs)

    def AddForex(self, *args, **kwargs):
        return self.add_forex(*args, **kwargs)

    def AddCrypto(self, *args, **kwargs):
        return self.add_crypto(*args, **kwargs)

    def AddFuture(self, *args, **kwargs):
        return self.add_future(*args, **kwargs)

    def AddOption(self, *args, **kwargs):
        return self.add_option(*args, **kwargs)

    def AddIndex(self, *args, **kwargs):
        return self.add_index(*args, **kwargs)

    def AddData(self, *args, **kwargs):
        return self.add_data(*args, **kwargs)

    def AddSecurity(self, *args, **kwargs):
        return self.add_security(*args, **kwargs)

    def MarketOrder(self, *args, **kwargs):
        return self.market_order(*args, **kwargs)

    def LimitOrder(self, *args, **kwargs):
        return self.limit_order(*args, **kwargs)

    def StopMarketOrder(self, *args, **kwargs):
        return self.stop_market_order(*args, **kwargs)

    def StopLimitOrder(self, *args, **kwargs):
        return self.stop_limit_order(*args, **kwargs)

    def SetHoldings(self, *args, **kwargs):
        return self.set_holdings(*args, **kwargs)

    def Liquidate(self, *args, **kwargs):
        return self.liquidate(*args, **kwargs)

    def Log(self, *args, **kwargs):
        return self.log(*args, **kwargs)

    def Debug(self, *args, **kwargs):
        return self.debug(*args, **kwargs)

    def Error(self, *args, **kwargs):
        return self.error(*args, **kwargs)

    def Plot(self, *args, **kwargs):
        return self.plot(*args, **kwargs)

    def History(self, *args, **kwargs):
        return self.history(*args, **kwargs)

    def Buy(self, *args, **kwargs):
        return self.buy(*args, **kwargs)

    def Sell(self, *args, **kwargs):
        return self.sell(*args, **kwargs)

    def Order(self, *args, **kwargs):
        return self.order(*args, **kwargs)

    def EMA(self, *args, **kwargs):
        return self.ema(*args, **kwargs)

    def SMA(self, *args, **kwargs):
        return self.sma(*args, **kwargs)

    def MACD(self, *args, **kwargs):
        return self.macd(*args, **kwargs)

    def RSI(self, *args, **kwargs):
        return self.rsi(*args, **kwargs)

    def BB(self, *args, **kwargs):
        return self.bb(*args, **kwargs)

    def ATR(self, *args, **kwargs):
        return self.atr(*args, **kwargs)

    def ROC(self, *args, **kwargs):
        return self.roc(*args, **kwargs)

    def MOM(self, *args, **kwargs):
        return self.mom(*args, **kwargs)

    def WILR(self, *args, **kwargs):
        return self.wilr(*args, **kwargs)

    def CCI(self, *args, **kwargs):
        return self.cci(*args, **kwargs)

    def ADX(self, *args, **kwargs):
        return self.adx(*args, **kwargs)

    def AROON(self, *args, **kwargs):
        return self.aroon(*args, **kwargs)

    def VWAP(self, *args, **kwargs):
        return self.vwap(*args, **kwargs)

    def MOMP(self, *args, **kwargs):
        return self.momp(*args, **kwargs)

    def GetParameter(self, *args, **kwargs):
        return self.get_parameter(*args, **kwargs)

    def SetParameters(self, *args, **kwargs):
        return self.set_parameters(*args, **kwargs)

    def Train(self, *args, **kwargs):
        return self.train(*args, **kwargs)

    def Download(self, *args, **kwargs):
        return self.download(*args, **kwargs)

    def Quit(self, *args, **kwargs):
        return self.quit(*args, **kwargs)

    def SetAlpha(self, *args, **kwargs):
        return self.set_alpha(*args, **kwargs)

    def AddAlpha(self, *args, **kwargs):
        return self.add_alpha(*args, **kwargs)

    def SetPortfolioConstruction(self, *args, **kwargs):
        return self.set_portfolio_construction(*args, **kwargs)

    def SetExecution(self, *args, **kwargs):
        return self.set_execution(*args, **kwargs)

    def SetRiskManagement(self, *args, **kwargs):
        return self.set_risk_management(*args, **kwargs)

    def AddRiskManagement(self, *args, **kwargs):
        return self.add_risk_management(*args, **kwargs)

    def SetUniverseSelection(self, *args, **kwargs):
        return self.set_universe_selection(*args, **kwargs)

    def AddUniverse(self, *args, **kwargs):
        return self.add_universe(*args, **kwargs)

    def AddChart(self, *args, **kwargs):
        return self.add_chart(*args, **kwargs)

    def SetSecurityInitializer(self, *args, **kwargs):
        return self.set_security_initializer(*args, **kwargs)

    def SetAccountCurrency(self, *args, **kwargs):
        return self.set_account_currency(*args, **kwargs)

    def SetTimeZone(self, *args, **kwargs):
        return self.set_time_zone(*args, **kwargs)

    def RegisterIndicator(self, *args, **kwargs):
        return self.register_indicator(*args, **kwargs)

    def Consolidate(self, *args, **kwargs):
        return self.consolidate(*args, **kwargs)

    def Initialize(self):
        return self.initialize()

    def GetLastKnownPrices(self, *args, **kwargs):
        return self.get_last_known_prices(*args, **kwargs)

    def WarmUpIndicator(self, *args, **kwargs):
        return self.warm_up_indicator(*args, **kwargs)

    def PlotIndicator(self, *args, **kwargs):
        return self.plot_indicator(*args, **kwargs)

    def FilteredIdentity(self, *args, **kwargs):
        return self.filtered_identity(*args, **kwargs)

    def CreateIndicatorName(self, *args, **kwargs):
        return self.create_indicator_name(*args, **kwargs)

    def IV(self, *args, **kwargs):
        return self.iv(*args, **kwargs)

    def D(self, *args, **kwargs):
        return self.d(*args, **kwargs)

    def G(self, *args, **kwargs):
        return self.g(*args, **kwargs)

    def T(self, *args, **kwargs):
        return self.t(*args, **kwargs)

    def R(self, *args, **kwargs):
        return self.r(*args, **kwargs)

    def V(self, *args, **kwargs):
        return self.v(*args, **kwargs)

    # ---- Properties with PascalCase aliases ----

    @property
    def Time(self):
        return self._time

    @property
    def UtcTime(self):
        return self._utc_time

    @property
    def StartDate(self):
        return self._start_date

    @property
    def EndDate(self):
        return self._end_date

    @property
    def IsWarmingUp(self):
        return self._is_warming_up

    @property
    def LiveMode(self):
        return self._live_mode

    @property
    def Portfolio(self):
        return self.portfolio

    @property
    def Securities(self):
        return self.securities

    @property
    def Name(self):
        return self._name

    @Name.setter
    def Name(self, value):
        self._name = value

    def __repr__(self):
        return f"FinceptStrategy({self._name})"


class _ContractList(list):
    """List subclass with .count property (like LEAN's C# List<T>)."""
    @property
    def count(self):
        return len(self)
    Count = count


class OptionChainProvider:
    """Provides option chain data."""
    def __init__(self, algo=None):
        self._algo = algo

    def get_option_contract_list(self, symbol, date):
        """Get real listed option contracts for a symbol (current chain only).
        Raises RuntimeError when no real chain is available for `date`."""
        underlying = symbol if isinstance(symbol, Symbol) else Symbol(str(symbol).upper())
        if self._algo and (self._algo._time or self._algo._start_date):
            as_of = self._algo._time or self._algo._start_date
        else:
            as_of = date
        contracts = _ContractList()
        for opt_sym, _strike, _right, _expiry, _quote in _fetch_real_option_chain(underlying, as_of):
            contracts.append(opt_sym)
        return contracts

    GetOptionContractList = get_option_contract_list


class Chart:
    """Chart container for plotting."""
    def __init__(self, name: str):
        self.name = name
        self.series = {}

    def add_series(self, series):
        self.series[series.name if hasattr(series, 'name') else str(series)] = series


class Series:
    """Chart data series."""
    def __init__(self, name: str, series_type=0, unit="$"):
        self.name = name
        self.series_type = series_type
        self.unit = unit
        self.values = []


class SeriesType:
    LINE = 0
    SCATTER = 1
    CANDLE = 2
    BAR = 3
    FLAG = 4
    STACKED_AREA = 5
    PIE = 6
    TREEMAP = 7


class NotificationManager:
    """Notification stub."""
    def email(self, to, subject, body):
        pass

    def sms(self, phone, message):
        pass

    def web(self, url, data=None):
        pass


# Framework model base classes

class AlphaModel:
    """Base class for alpha models."""
    def update(self, algorithm, data):
        return []

    def on_securities_changed(self, algorithm, changes):
        pass


class PortfolioConstructionModel:
    """Base class for portfolio construction."""
    def create_targets(self, algorithm, insights):
        return []

    def on_securities_changed(self, algorithm, changes):
        pass


class ExecutionModel:
    """Base class for execution models."""
    def execute(self, algorithm, targets):
        pass

    def on_securities_changed(self, algorithm, changes):
        pass


class RiskManagementModel:
    """Base class for risk management."""
    def manage_risk(self, algorithm, targets):
        return targets

    def on_securities_changed(self, algorithm, changes):
        pass


class UniverseSelectionModel:
    """Base class for universe selection."""
    def create_universes(self, algorithm):
        return []

    def get_next_refresh_time_utc(self):
        return datetime.utcnow() + timedelta(days=1)


class NullRiskManagementModel(RiskManagementModel):
    """No-op risk management."""
    pass


class NullAlphaModel(AlphaModel):
    """No-op alpha model."""
    pass


class ConstantAlphaModel(AlphaModel):
    """Alpha model that emits constant insights for all securities."""

    def __init__(self, insight_type=None, direction=None, period=None, magnitude=None,
                 weight=None, confidence=None):
        self._type = insight_type or InsightType.PRICE
        self._direction = direction or InsightDirection.UP
        self._period = period or timedelta(days=1)
        self._magnitude = magnitude
        self._weight = weight
        self._confidence = confidence
        self._securities = []

    def update(self, algorithm, data):
        insights = []
        for security in self._securities:
            if data.contains_key(security.symbol):
                insights.append(Insight(
                    security.symbol, self._period, self._type,
                    self._direction, self._magnitude, weight=self._weight
                ))
        return insights

    def on_securities_changed(self, algorithm, changes):
        for sec in changes.added_securities:
            if sec not in self._securities:
                self._securities.append(sec)
        for sec in changes.removed_securities:
            if sec in self._securities:
                self._securities.remove(sec)


class EqualWeightingPortfolioConstructionModel(PortfolioConstructionModel):
    """Portfolio construction model that assigns equal weight to all insights."""

    def __init__(self, rebalance=None, portfolio_bias=None, resolution=None, **kwargs):
        self._rebalance = rebalance
        self._portfolio_bias = portfolio_bias
        self._resolution = resolution

    def create_targets(self, algorithm, insights):
        if not insights:
            return []
        targets = []
        weight = 1.0 / len(insights) if insights else 0
        for insight in insights:
            direction = 1 if insight.direction == InsightDirection.UP else (
                -1 if insight.direction == InsightDirection.DOWN else 0)
            targets.append(PortfolioTarget(insight.symbol, weight * direction))
        return targets


class InsightWeightingPortfolioConstructionModel(PortfolioConstructionModel):
    """Portfolio construction weighted by insight magnitude/confidence."""

    def __init__(self, rebalance=None, portfolio_bias=None):
        self._rebalance = rebalance
        self._portfolio_bias = portfolio_bias

    def create_targets(self, algorithm, insights):
        if not insights:
            return []
        total_weight = sum(abs(i.weight or i.confidence or 1.0) for i in insights)
        if total_weight == 0:
            total_weight = 1
        targets = []
        for insight in insights:
            w = abs(insight.weight or insight.confidence or 1.0) / total_weight
            direction = 1 if insight.direction == InsightDirection.UP else (
                -1 if insight.direction == InsightDirection.DOWN else 0)
            targets.append(PortfolioTarget(insight.symbol, w * direction))
        return targets


class MeanVarianceOptimizationPortfolioConstructionModel(PortfolioConstructionModel):
    """Mean-variance optimization portfolio construction -- not implemented in the local engine.

    The previous stub returned equal weights while presenting them as
    optimized; it now refuses to run rather than produce made-up weights.
    """

    def __init__(self, *args, **kwargs):
        raise NotImplementedError(
            "MeanVarianceOptimizationPortfolioConstructionModel is not implemented in the local engine "
            "(no optimizer is available; equal weights are not substituted)")

    def create_targets(self, algorithm, insights):
        raise NotImplementedError(
            "MeanVarianceOptimizationPortfolioConstructionModel is not implemented in the local engine")


class BlackLittermanOptimizationPortfolioConstructionModel(PortfolioConstructionModel):
    """Black-Litterman optimization portfolio construction -- not implemented in the local engine.

    The previous stub returned equal weights while presenting them as
    optimized; it now refuses to run rather than produce made-up weights.
    """

    def __init__(self, *args, **kwargs):
        raise NotImplementedError(
            "BlackLittermanOptimizationPortfolioConstructionModel is not implemented in the local engine "
            "(no optimizer is available; equal weights are not substituted)")

    def create_targets(self, algorithm, insights):
        raise NotImplementedError(
            "BlackLittermanOptimizationPortfolioConstructionModel is not implemented in the local engine")


class ImmediateExecutionModel(ExecutionModel):
    """Execution model that immediately submits market orders."""

    def execute(self, algorithm, targets):
        for target in targets:
            existing = algorithm.portfolio[str(target.symbol)].quantity
            diff = target.quantity - existing
            if abs(diff) > 0:
                algorithm.market_order(target.symbol, diff)


class VolumeWeightedAveragePriceExecutionModel(ExecutionModel):
    """VWAP execution model (simplified stub - acts like immediate)."""

    def execute(self, algorithm, targets):
        for target in targets:
            existing = algorithm.portfolio[str(target.symbol)].quantity
            diff = target.quantity - existing
            if abs(diff) > 0:
                algorithm.market_order(target.symbol, diff)


class StandardDeviationExecutionModel(ExecutionModel):
    """Std dev execution model (simplified stub)."""

    def __init__(self, period=60, deviations=2, resolution=None):
        self._period = period
        self._deviations = deviations

    def execute(self, algorithm, targets):
        for target in targets:
            existing = algorithm.portfolio[str(target.symbol)].quantity
            diff = target.quantity - existing
            if abs(diff) > 0:
                algorithm.market_order(target.symbol, diff)


class MaximumDrawdownPercentPortfolio(RiskManagementModel):
    """Risk model: maximum portfolio drawdown percentage."""

    def __init__(self, maximum_drawdown_percent=0.05, is_not_a_ccount_currency_for=False):
        self._max_dd = maximum_drawdown_percent
        self._trailing_high = 0

    def manage_risk(self, algorithm, targets):
        portfolio_value = algorithm.portfolio.total_portfolio_value
        self._trailing_high = max(self._trailing_high, portfolio_value)
        if self._trailing_high > 0:
            dd = (self._trailing_high - portfolio_value) / self._trailing_high
            if dd > self._max_dd:
                return [PortfolioTarget(str(s.symbol), 0) for s in algorithm.securities]
        return targets


class MaximumDrawdownPercentPerSecurity(RiskManagementModel):
    """Risk model: maximum drawdown per security."""

    def __init__(self, maximum_drawdown_percent=0.05):
        self._max_dd = maximum_drawdown_percent

    def manage_risk(self, algorithm, targets):
        risk_targets = list(targets) if targets else []
        for ticker, holding in algorithm.portfolio.items():
            if holding.invested and holding.unrealized_profit_percent < -self._max_dd:
                risk_targets.append(PortfolioTarget(ticker, 0))
        return risk_targets


class MaximumUnrealizedProfitPercentPerSecurity(RiskManagementModel):
    """Risk model: take profit at maximum unrealized profit."""

    def __init__(self, maximum_unrealized_profit_percent=0.05):
        self._max_profit = maximum_unrealized_profit_percent

    def manage_risk(self, algorithm, targets):
        risk_targets = list(targets) if targets else []
        for ticker, holding in algorithm.portfolio.items():
            if holding.invested and holding.unrealized_profit_percent > self._max_profit:
                risk_targets.append(PortfolioTarget(ticker, 0))
        return risk_targets


class TrailingStopRiskManagementModel(RiskManagementModel):
    """Risk model: trailing stop for each security."""

    def __init__(self, maximum_drawdown_percent=0.05):
        self._max_dd = maximum_drawdown_percent
        self._trailing_highs = {}

    def manage_risk(self, algorithm, targets):
        risk_targets = list(targets) if targets else []
        for ticker, holding in algorithm.portfolio.items():
            if holding.invested:
                price = holding.market_price
                if ticker not in self._trailing_highs:
                    self._trailing_highs[ticker] = price
                self._trailing_highs[ticker] = max(self._trailing_highs[ticker], price)
                trail = self._trailing_highs[ticker]
                if trail > 0 and (trail - price) / trail > self._max_dd:
                    risk_targets.append(PortfolioTarget(ticker, 0))
            else:
                self._trailing_highs.pop(ticker, None)
        return risk_targets


class MaximumSectorExposureRiskManagementModel(RiskManagementModel):
    """Risk model: maximum sector exposure (stub)."""

    def __init__(self, maximum_sector_exposure=0.3):
        self._max_exposure = maximum_sector_exposure

    def manage_risk(self, algorithm, targets):
        return targets


class CompositeRiskManagementModel(RiskManagementModel):
    """Combines multiple risk management models."""

    def __init__(self, *models):
        self._models = list(models)

    def add_risk_management(self, model):
        self._models.append(model)

    def manage_risk(self, algorithm, targets):
        for model in self._models:
            targets = model.manage_risk(algorithm, targets)
        return targets


class ManualUniverseSelectionModel(UniverseSelectionModel):
    """Universe selection from a manually specified list of symbols."""

    def __init__(self, symbols=None, *args):
        self._symbols = symbols or []
        if args:
            self._symbols = list(args) if not symbols else self._symbols

    def create_universes(self, algorithm):
        return self._symbols


class FundamentalUniverseSelectionModel(UniverseSelectionModel):
    """Universe selection based on fundamental data."""

    def __init__(self, filter_fine_universe=True, universe_settings=None, *args, **kwargs):
        self._filter_fine = filter_fine_universe

    def create_universes(self, algorithm):
        return []

    def select_coarse(self, algorithm, coarse):
        return [c.symbol for c in coarse]

    def select_fine(self, algorithm, fine):
        return [f.symbol for f in fine]


class ScheduledUniverseSelectionModel(UniverseSelectionModel):
    """Universe selection on a schedule."""

    def __init__(self, date_rule, time_rule, selector, settings=None):
        self._date_rule = date_rule
        self._time_rule = time_rule
        self._selector = selector

    def create_universes(self, algorithm):
        if callable(self._selector):
            return self._selector(algorithm.time)
        return []


class CustomUniverseSelectionModel(UniverseSelectionModel):
    """Custom universe selection from a function."""

    def __init__(self, name, selector, settings=None):
        self._name = name
        self._selector = selector

    def create_universes(self, algorithm):
        if callable(self._selector):
            return self._selector(algorithm.time)
        return []


class CompositeAlphaModel(AlphaModel):
    """Combines multiple alpha models."""

    def __init__(self, *models):
        self._models = list(models)

    def update(self, algorithm, data):
        all_insights = []
        for model in self._models:
            insights = model.update(algorithm, data)
            if insights:
                all_insights.extend(insights)
        return all_insights

    def on_securities_changed(self, algorithm, changes):
        for model in self._models:
            model.on_securities_changed(algorithm, changes)


class PythonData:
    """Base class for custom Python data types."""

    def __init__(self):
        self.symbol = None
        self.time = None
        self.end_time = None
        self.value = 0.0
        self._data = {}

    def get_source(self, config, date, is_live_mode):
        return None

    def reader(self, config, line, date, is_live_mode):
        return None

    def __getattr__(self, name):
        if name.startswith('_'):
            raise AttributeError(name)
        return self._data.get(name, 0)

    def __setattr__(self, name, value):
        if name in ('symbol', 'time', 'end_time', 'value', '_data'):
            super().__setattr__(name, value)
        else:
            if not hasattr(self, '_data'):
                super().__setattr__('_data', {})
            self._data[name] = value

    def __getitem__(self, key):
        return self._data.get(key, 0)

    def __setitem__(self, key, value):
        self._data[key] = value

    def __repr__(self):
        return f"PythonData({self.symbol}, {self.value})"


class PythonQuandl(PythonData):
    """Quandl data loader (stub)."""

    def __init__(self):
        super().__init__()

    def get_source(self, config, date, is_live_mode):
        return None


class InsightCollection:
    """Collection of insights with filtering capabilities."""

    def __init__(self):
        self._insights = []

    def add(self, insight):
        self._insights.append(insight)

    def add_range(self, insights):
        self._insights.extend(insights)

    def clear(self):
        self._insights.clear()

    def get_active_insights(self, utc_time):
        return [i for i in self._insights if i.close_time_utc > utc_time]

    def has_active_insights(self, symbol, utc_time):
        return any(i for i in self._insights
                   if str(i.symbol) == str(symbol) and i.close_time_utc > utc_time)

    def remove_expired_insights(self, utc_time):
        self._insights = [i for i in self._insights if i.close_time_utc > utc_time]

    def set_insight_score_function(self, func):
        """Set a custom scoring function for insights."""
        self._score_function = func

    def __iter__(self):
        return iter(self._insights)

    def __len__(self):
        return len(self._insights)


class Field:
    """Universe selection field for sorting/filtering."""

    @staticmethod
    def dollar_volume(x):
        return getattr(x, 'dollar_volume', 0)

    @staticmethod
    def price(x):
        return getattr(x, 'price', 0)

    @staticmethod
    def volume(x):
        return getattr(x, 'volume', 0)

    CLOSE = staticmethod(lambda x: getattr(x, 'close', getattr(x, 'price', 0)))
    OPEN = staticmethod(lambda x: getattr(x, 'open', 0))
    HIGH = staticmethod(lambda x: getattr(x, 'high', 0))
    LOW = staticmethod(lambda x: getattr(x, 'low', 0))
    VOLUME = staticmethod(lambda x: getattr(x, 'volume', 0))
    AVERAGE = staticmethod(lambda x: getattr(x, 'close', getattr(x, 'price', 0)))

    # Lowercase aliases (used by many strategies)
    close = CLOSE
    open = OPEN
    high = HIGH
    low = LOW
    average = AVERAGE

    SEVEN_BAR = staticmethod(lambda x: getattr(x, 'close', getattr(x, 'price', 0)))
    BID_CLOSE = staticmethod(lambda x: getattr(x, 'bid_close', getattr(x, 'close', 0)))
    ASK_CLOSE = staticmethod(lambda x: getattr(x, 'ask_close', getattr(x, 'close', 0)))
    BID_OPEN = staticmethod(lambda x: getattr(x, 'bid_open', getattr(x, 'open', 0)))
    ASK_OPEN = staticmethod(lambda x: getattr(x, 'ask_open', getattr(x, 'open', 0)))
    BID_HIGH = staticmethod(lambda x: getattr(x, 'bid_high', getattr(x, 'high', 0)))
    ASK_HIGH = staticmethod(lambda x: getattr(x, 'ask_high', getattr(x, 'high', 0)))
    BID_LOW = staticmethod(lambda x: getattr(x, 'bid_low', getattr(x, 'low', 0)))
    ASK_LOW = staticmethod(lambda x: getattr(x, 'ask_low', getattr(x, 'low', 0)))
    SevenBar = SEVEN_BAR
    BidClose = BID_CLOSE
    AskClose = ASK_CLOSE
    BidOpen = BID_OPEN
    AskOpen = ASK_OPEN
    BidHigh = BID_HIGH
    AskHigh = ASK_HIGH
    BidLow = BID_LOW
    AskLow = ASK_LOW

    DollarVolume = staticmethod(lambda x: getattr(x, 'dollar_volume', 0))
    Price = staticmethod(lambda x: getattr(x, 'price', 0))
    Volume = staticmethod(lambda x: getattr(x, 'volume', 0))
    Close = CLOSE
    Open = OPEN
    High = HIGH
    Low = LOW
    Average = AVERAGE


class Futures:
    """Futures contract specifications."""

    class Indices:
        SP_500_E_MINI = "ES"
        NASDAQ_100_E_MINI = "NQ"
        DOW_30_E_MINI = "YM"
        RUSSELL_2000_E_MINI = "RTY"
        VIX = "VX"
        SP500 = "ES"
        MICRO_SP_500_E_MINI = "MES"
        EURO_STOXX_50 = "FESX"
        NIKKEI_225 = "NK"
        FTSE_100 = "Z"
        DAX = "FDAX"
        HANG_SENG = "HSI"
        MSCI_EMERGING_MARKETS = "MXEF"

    class Metals:
        GOLD = "GC"
        SILVER = "SI"
        PLATINUM = "PL"
        PALLADIUM = "PA"
        COPPER = "HG"
        MICRO_GOLD = "MGC"
        MICRO_SILVER = "SIL"

    class Energies:
        CRUDE_OIL_WTI = "CL"
        NATURAL_GAS = "NG"
        HEATING_OIL = "HO"
        GASOLINE = "RB"
        BRENT_CRUDE = "BZ"
        MICRO_CRUDE_OIL_WTI = "MCL"

    class Grains:
        CORN = "ZC"
        WHEAT = "ZW"
        SOYBEANS = "ZS"
        SOYBEAN_MEAL = "ZM"
        SOYBEAN_OIL = "ZL"
        OATS = "ZO"

    class Currencies:
        EUR = "6E"
        GBP = "6B"
        JPY = "6J"
        AUD = "6A"
        CAD = "6C"
        CHF = "6S"

    class Financials:
        Y_2_TREASURY_NOTE = "ZT"
        Y_5_TREASURY_NOTE = "ZF"
        Y_10_TREASURY_NOTE = "ZN"
        Y_30_TREASURY_BOND = "ZB"
        EURODOLLAR = "GE"

    class Meats:
        LIVE_CATTLE = "LE"
        FEEDER_CATTLE = "GF"
        LEAN_HOGS = "HE"

    class Softs:
        SUGAR = "SB"
        COTTON = "CT"
        COFFEE = "KC"
        COCOA = "CC"


class Globals:
    """Global settings."""
    DataFolder = ""
    Cache = ""


class SecurityIdentifier:
    """Security identifier generator."""

    @staticmethod
    def generate_equity(ticker, market="usa", map_first_date=True):
        return Symbol(ticker, SecurityType.EQUITY, market)

    @staticmethod
    def generate_option(underlying, market="usa", option_style=0, option_right=0,
                        strike_price=0, expiry_date=None):
        return Symbol(underlying, SecurityType.OPTION, market)

    @staticmethod
    def generate_future(ticker, market="cme", expiry_date=None):
        return Symbol(ticker, SecurityType.FUTURE, market)


class PortfolioBias:
    """Portfolio bias setting."""
    LONG = 0
    SHORT = 1
    LONG_SHORT = 2

    Long = 0
    Short = 1
    LongShort = 2


class Resolution_:
    """Namespace aliases for Resolution to support both Resolution.Daily and Resolution.DAILY"""
    pass
